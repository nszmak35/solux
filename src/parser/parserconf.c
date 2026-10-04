#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "parserconf.h"

static PyObject *stringify(PyObject *value);

typedef struct {
	DnxEntryCallback cb;
	void *userdata;
	int source;
	int emitted;
} Parser;

static int is_csv_key(const char *key)
{
	return !strcmp(key, "fullscreen_bg") || !strncmp(key, "animation_curve_", 16);
}

static PyObject *stringify_sequence(PyObject *value)
{
	Py_ssize_t n = PySequence_Size(value);
	PyObject *parts;
	PyObject *sep;
	PyObject *result;
	if (n < 0) return NULL;
	parts = PyList_New(n);
	if (!parts) return NULL;
	for (Py_ssize_t i = 0; i < n; i++) {
		PyObject *item = PySequence_GetItem(value, i);
		PyObject *s;
		if (!item) { Py_DECREF(parts); return NULL; }
		s = stringify(item);
		Py_DECREF(item);
		if (!s || PyList_SetItem(parts, i, s) < 0) {
			Py_XDECREF(s); Py_DECREF(parts); return NULL;
		}
	}
	sep = PyUnicode_FromString(", ");
	if (!sep) { Py_DECREF(parts); return NULL; }
	result = PyUnicode_Join(sep, parts);
	Py_DECREF(sep);
	Py_DECREF(parts);
	return result;
}

static PyObject *stringify(PyObject *value)
{
	if (value == Py_None)
		return PyUnicode_FromString("NULL");
	if (PyBool_Check(value))
		return PyUnicode_FromString(value == Py_True ? "1" : "0");
	return PyObject_Str(value);
}

static int emit_scalar(Parser *p, const char *key, PyObject *value)
{
	PyObject *s;
	const char *text;
	DnxEntry e;
	int rc;

	if ((PyList_Check(value) || PyTuple_Check(value)) && is_csv_key(key))
		s = stringify_sequence(value);
	else
		s = stringify(value);
	if (!s) return -1;
	text = PyUnicode_AsUTF8(s);
	if (!text) { Py_DECREF(s); return -1; }

	memset(&e, 0, sizeof(e));
	e.type = DNX_SCALAR;
	e.key = key;
	e.value = text;
	e.source = p->source;
	p->emitted++;
	rc = p->cb(&e, p->userdata);
	Py_DECREF(s);
	return rc;
}

static int emit_row(Parser *p, DnxType type, const char *key, PyObject *seq)
{
	Py_ssize_t n, i;
	const char **items;
	PyObject **strings;
	DnxEntry e;
	int rc;

	if (!PyList_Check(seq) && !PyTuple_Check(seq))
		return 0;
	n = PySequence_Size(seq);
	if (n <= 0) return 0;

	items = calloc((size_t)n, sizeof(*items));
	strings = calloc((size_t)n, sizeof(*strings));
	if (!items || !strings) { free(items); free(strings); return -1; }

	for (i = 0; i < n; i++) {
		PyObject *v = PySequence_GetItem(seq, i);
		if (!v) { rc = -1; goto out; }
		strings[i] = stringify(v);
		Py_DECREF(v);
		if (!strings[i]) { rc = -1; goto out; }
		items[i] = PyUnicode_AsUTF8(strings[i]);
		if (!items[i]) { rc = -1; goto out; }
	}

	memset(&e, 0, sizeof(e));
	e.type = type;
	e.key = key;
	e.items = items;
	e.count = (size_t)n;
	e.source = p->source;
	p->emitted++;
	rc = p->cb(&e, p->userdata);

out:
	for (i = 0; i < n; i++) Py_XDECREF(strings[i]);
	free(strings);
	free(items);
	return rc;
}

static int emit_rules(Parser *p, PyObject *value)
{
    Py_ssize_t n, i;
    if (!PyList_Check(value) && !PyTuple_Check(value)) return 0;
    n = PySequence_Size(value);
    for (i = 0; i < n; i++) {
        PyObject *item = PySequence_GetItem(value, i);
        PyObject *row = NULL;
        PyObject *v;
        int rc;
        if (!item) return -1;
        if (!PyDict_Check(item)) {
            Py_DECREF(item);
            continue;
        }
        /* New rules format:
         * {"appid": str, "workspace": int, "floating": bool,
         *  "fullscreen": bool, "monitor": int|string}
         * Missing values are represented by the runtime defaults. */
        row = PyTuple_New(5);
        if (!row) { Py_DECREF(item); return -1; }

        v = PyDict_GetItemString(item, "appid");
        if (!v) v = Py_None;
        Py_INCREF(v); PyTuple_SET_ITEM(row, 0, v);
        v = PyDict_GetItemString(item, "workspace");
        if (!v) v = PyLong_FromLong(0); else Py_INCREF(v);
        PyTuple_SET_ITEM(row, 1, v);
        v = PyDict_GetItemString(item, "floating");
        if (!v) v = Py_False; else Py_INCREF(v);
        PyTuple_SET_ITEM(row, 2, v);
        v = PyDict_GetItemString(item, "fullscreen");
        if (!v) v = Py_False; else Py_INCREF(v);
        PyTuple_SET_ITEM(row, 3, v);
        v = PyDict_GetItemString(item, "monitor");
        if (!v) v = PyLong_FromLong(-1); else Py_INCREF(v);
        PyTuple_SET_ITEM(row, 4, v);

        rc = emit_row(p, DNX_TABLE, "rules", row);
        Py_DECREF(row);
        Py_DECREF(item);
        if (rc) return rc;
    }
    return 0;
}

static int emit_list(Parser *p, const char *key, PyObject *value)
{
	Py_ssize_t n, i;
	if (!PyList_Check(value) && !PyTuple_Check(value)) return 0;
	n = PySequence_Size(value);
	for (i = 0; i < n; i++) {
		PyObject *item = PySequence_GetItem(value, i);
		int rc;
		if (!item) return -1;
		/* tags and simple lists use one value per entry. */
		if (!PyList_Check(item) && !PyTuple_Check(item)) {
			PyObject *row = PyTuple_Pack(1, item);
			Py_DECREF(item);
			if (!row) return -1;
			rc = emit_row(p, DNX_LIST, key, row);
			Py_DECREF(row);
		} else {
			rc = emit_row(p, DNX_TABLE, key, item);
			Py_DECREF(item);
		}
		if (rc) return rc;
	}
	return 0;
}

static int emit_class(Parser *p, PyObject *klass)
{
	PyObject *mapping = PyObject_GetAttrString(klass, "__dict__");
	PyObject *keys;
	Py_ssize_t n, i;
	int rc = 0;

	if (!mapping) { PyErr_Clear(); return 0; }
	keys = PyMapping_Keys(mapping);
	if (!keys) { Py_DECREF(mapping); return -1; }
	n = PySequence_Size(keys);
	for (i = 0; i < n; i++) {
		PyObject *key = PySequence_GetItem(keys, i);
		PyObject *value;
		const char *name;
		if (!key) { rc = -1; break; }
		name = PyUnicode_Check(key) ? PyUnicode_AsUTF8(key) : NULL;
		if (!name || name[0] == '_') { Py_DECREF(key); continue; }
		value = PyObject_GetItem(mapping, key);
		Py_DECREF(key);
		if (!value) { PyErr_Clear(); continue; }
		/* Most class attributes must be scalar configuration values.
		 * The exception is the CSV-style settings: Python users may write
		 * fullscreen_bg / animation_curve_* either as a string or as a
		 * list/tuple of numbers.  DNX accepted the resulting comma-separated
		 * representation, so keep both forms equivalent. */
		if (PyList_Check(value) || PyTuple_Check(value)) {
			if (is_csv_key(name))
				rc = emit_scalar(p, name, value);
			else
				rc = 0;
			Py_DECREF(value);
			if (rc) break;
			continue;
		}
		if (PyDict_Check(value)) {
			Py_DECREF(value);
			continue;
		}
		rc = emit_scalar(p, name, value);
		Py_DECREF(value);
		if (rc) break;
	}
	Py_DECREF(keys);
	Py_DECREF(mapping);
	return rc;
}

static int parse_globals(Parser *p, PyObject *globals)
{
	PyObject *key, *value;
	Py_ssize_t pos = 0;

	while (PyDict_Next(globals, &pos, &key, &value)) {
		const char *name;
		if (!PyUnicode_Check(key)) continue;
		name = PyUnicode_AsUTF8(key);
		if (!name || name[0] == '_') continue;

		if (PyType_Check(value)) {
			if (emit_class(p, value)) return -1;
			continue;
		}
		if (!strcmp(name, "tags")) {
			if (PyLong_Check(value) && !PyBool_Check(value)) {
				long count = PyLong_AsLong(value);
				if (PyErr_Occurred()) { PyErr_Clear(); return -1; }
				if (count < 0) count = 0;
				if (count > 31) count = 31;
				for (long i = 1; i <= count; i++) {
					PyObject *s = PyUnicode_FromFormat("%ld", i);
					PyObject *row;
					int rc;
					if (!s) return -1;
					row = PyTuple_Pack(1, s);
					Py_DECREF(s);
					if (!row) return -1;
					rc = emit_row(p, DNX_LIST, name, row);
					Py_DECREF(row);
					if (rc) return rc;
				}
			} else if (PyList_Check(value) || PyTuple_Check(value)) {
				if (emit_list(p, name, value)) return -1;
			}
			continue;
		}
		if (!strcmp(name, "layouts") && PyDict_Check(value)) {
			PyObject *lk, *lv;
			Py_ssize_t lp = 0;
			while (PyDict_Next(value, &lp, &lk, &lv)) {
				PyObject *row = Py_BuildValue("(OO)", lk, lv);
				int rc;
				if (!row) return -1;
				rc = emit_row(p, DNX_TABLE, name, row);
				Py_DECREF(row);
				if (rc) return rc;
			}
			continue;
		}
		if (!strcmp(name, "autostart")) {
			if (!PyList_Check(value) && !PyTuple_Check(value)) continue;
			Py_ssize_t n = PySequence_Size(value);
			for (Py_ssize_t i = 0; i < n; i++) {
				PyObject *item = PySequence_GetItem(value, i);
				PyObject *row;
				int rc;
				if (!item) return -1;
				if (!PyUnicode_Check(item)) {
					Py_DECREF(item);
					continue;
				}
				row = PyTuple_Pack(1, item);
				Py_DECREF(item);
				if (!row) return -1;
				rc = emit_row(p, DNX_TABLE, name, row);
				Py_DECREF(row);
				if (rc) return rc;
			}
			continue;
		}
		if (!strcmp(name, "rules")) {
			if (emit_rules(p, value)) return -1;
			continue;
		}
		if (!strcmp(name, "monrules") || !strcmp(name, "monitors") ||
		    !strcmp(name, "keys") || !strcmp(name, "buttons") || !strcmp(name, "gestures")) {
			if (PyList_Check(value) || PyTuple_Check(value)) {
				if (emit_list(p, name, value)) return -1;
			}
			continue;
		}
		if (PyList_Check(value) || PyTuple_Check(value) || PyDict_Check(value)) continue;
		if (emit_scalar(p, name, value)) return -1;
	}
	return 0;
}

static int parse_file(Parser *p, const char *path)
{
	FILE *fp;
	long size;
	char *source;
	PyObject *globals, *code, *result;
	int rc;

	if (!path || access(path, R_OK) != 0) return 0;
	fp = fopen(path, "rb");
	if (!fp) return 0;
	if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
	size = ftell(fp);
	if (size < 0 || size > INT_MAX) { fclose(fp); return -1; }
	if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return -1; }
	source = malloc((size_t)size + 1);
	if (!source) { fclose(fp); return -1; }
	if (fread(source, 1, (size_t)size, fp) != (size_t)size) {
		free(source); fclose(fp); return -1;
	}
	source[size] = '\0';
	fclose(fp);

	globals = PyDict_New();
	if (!globals) { free(source); return -1; }
	if (PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins()) < 0) {
		Py_DECREF(globals); free(source); return -1;
	}
	code = Py_CompileStringExFlags(source, path, Py_file_input, NULL, -1);
	free(source);
	if (!code) { PyErr_Print(); Py_DECREF(globals); return -1; }
	result = PyEval_EvalCode(code, globals, globals);
	Py_DECREF(code);
	if (!result) {
		PyErr_Print(); Py_DECREF(globals); return -1;
	}
	Py_DECREF(result);
	rc = parse_globals(p, globals);
	if (rc < 0) PyErr_Print();
	Py_DECREF(globals);
	return rc;
}

static int validate_callback(const DnxEntry *entry, void *userdata)
{
	(void)entry;
	(void)userdata;
	return 0;
}

int dnx_validate_ff(const char *default_path, const char *user_path)
{
	Parser p;
	int rc;

	if (!Py_IsInitialized())
		Py_Initialize();

	memset(&p, 0, sizeof(p));
	p.cb = validate_callback;
	p.userdata = NULL;
	p.source = 0;

	/* Missing files are allowed; parse errors are not. */
	if (default_path && access(default_path, R_OK) == 0) {
		rc = parse_file(&p, default_path);
		if (rc < 0)
			return 0;
	}

	if (user_path && (!default_path || strcmp(user_path, default_path) != 0) &&
			access(user_path, R_OK) == 0) {
		p.source = 1;
		rc = parse_file(&p, user_path);
		if (rc < 0)
			return 0;
	}

	return 1;
}

int dnx_foreach_ff(const char *default_path, const char *user_path,
			   DnxEntryCallback callback, void *userdata)
{
	Parser p;
	int rc;
	if (!callback) return 0;
	if (!Py_IsInitialized()) Py_Initialize();

	memset(&p, 0, sizeof(p));
	p.cb = callback;
	p.userdata = userdata;
	p.source = 0;
	rc = parse_file(&p, default_path);
	if (rc < 0) return 0;

	if (user_path && (!default_path || strcmp(user_path, default_path) != 0) && access(user_path, R_OK) == 0) {
		p.source = 1;
		rc = parse_file(&p, user_path);
		if (rc < 0) return 0;
	}
	return p.emitted > 0;
}

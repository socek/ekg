/* $Id$ */

/*
 *  (C) Copyright 2002 Wojtek Kaniewski <wojtekka@irc.pl>
 *                     Robert J. Wo�ny <speedy@ziew.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License Version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <Python.h>
#include "xmalloc.h"
#include "stuff.h"
#include "themes.h"
#include "commands.h"
#include "version.h"
#include "vars.h"
#include "python.h"

static PyObject* ekg_connect(PyObject *self, PyObject *args)
{
	do_connect();

	return Py_BuildValue("");
}

static PyObject* ekg_disconnect(PyObject *self, PyObject *args)
{
	char *reason = NULL;

	if (!PyArg_ParseTuple(args, "|s", &reason))
		return NULL;

	command_exec("disconnect", reason);

	return Py_BuildValue("");
}

static PyObject* ekg_printf(PyObject *self, PyObject *pyargs)
{
	char *format = "generic", *args[9];
	int i;

	for (i = 0; i < 9; i++)
		args[i] = "";

	if (!PyArg_ParseTuple(pyargs, "s|sssssssss:printf", &format, &args[0], &args[1], &args[2], &args[3], &args[4], &args[5], &args[6], &args[7], &args[8]))
		return NULL;

	print(format, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]);

	return Py_BuildValue("");
}

#if 0
static PyObject* ekg_print(PyObject *self, PyObject *pyargs)
{
	char *arg;

	if (!PyArg_ParseTuple(pyargs, "s:print", &arg))
		return NULL;

	print("generic", arg);

	return Py_BuildValue("");
}
#endif

static PyObject* ekg_command(PyObject *self, PyObject *args)
{
	char *command = NULL;

	if (!PyArg_ParseTuple(args, "s", &command))
		return NULL;

	command_exec(NULL, command);

	return Py_BuildValue("");
}

static PyMethodDef ekg_methods[] = {
	{ "connect", ekg_connect, METH_VARARGS, "" },
	{ "disconnect", ekg_disconnect, METH_VARARGS, "" },
	{ "printf", ekg_printf, METH_VARARGS, "" },
	{ "command", ekg_command, METH_VARARGS, "" },
	{ NULL, NULL, 0, NULL }
};

static PyObject *ekg_config_getattr(PyObject *o, char *name)
{
	list_t l;

	for (l = variables; l; l = l->next) {
		struct variable *v = l->data;

		if (!strcmp(v->name, name)) {
			if (v->type == VAR_BOOL || v->type == VAR_INT)
				return Py_BuildValue("i", *(int*)(v->ptr));
			else
				return Py_BuildValue("s", *(char**)(v->ptr));
		}
	}

	return NULL;
}

static int ekg_config_setattr(PyObject *o, char *name, PyObject *value)
{
	struct variable *v = variable_find(name);

	if (!v) {
		PyErr_SetString(PyExc_LookupError, "unknown variable");
		return -1;
	}

	if (v->type == VAR_INT || v->type == VAR_BOOL) {
		if (!PyInt_Check(value)) {
			PyErr_SetString(PyExc_TypeError, "invalid type");
			return -1;
		}
		variable_set(name, itoa(PyInt_AsLong(value)), 0);
	} else {
		if (!PyString_Check(value)) {
			PyErr_SetString(PyExc_TypeError, "invalid type");
			return -1;
		}
		variable_set(name, PyString_AsString(value), 0);
	}

	return 0;
}

static void ekg_config_dealloc(PyObject *o)
{

}

static PyTypeObject ekg_config_type = {
	PyObject_HEAD_INIT(&PyType_Type)
	0,
	"config",
	sizeof(PyObject),
	0,
	ekg_config_dealloc,
	0,
	ekg_config_getattr,
	ekg_config_setattr,
};

int python_initialize()
{
	PyObject *ekg, *ekg_config;

	/* PyImport_ImportModule spodziewa si� nazwy modu�u, kt�ry znajduje
	 * si� w $PYTHONPATH, wi�c dodajemy tam katalog ~/.gg/scripts. mo�na
	 * to zrobi� w bardziej elegancki spos�b, ale po co komplikowa� sobie
	 * �ycie? */

	if (getenv("PYTHONPATH")) {
		char *tmp = saprintf("%s:%s", getenv("PYTHONPATH"), prepare_path("scripts", 0));
		setenv("PYTHONPATH", tmp, 1);
		xfree(tmp);
	} else
		setenv("PYTHONPATH", prepare_path("scripts", 0), 1);

	Py_Initialize();

	PyImport_AddModule("ekg");

	if (!(ekg = Py_InitModule("ekg", ekg_methods)))
		return -1;

	PyModule_AddStringConstant(ekg, "version", VERSION);

	ekg_config = PyObject_NEW(PyObject, &ekg_config_type);
	PyModule_AddObject(ekg, "config", ekg_config);

	return 0;
}

/*
 * python_finalize()
 *
 * usuwa z pami�ci interpreter, zwalnia pami�� itd.
 *
 * 0/-1
 */
int python_finalize()
{
	list_t l;

	for (l = modules; l; l = l->next) {
		struct module *m = l->data;

		xfree(m->name);

		if (m->deinit) {
			PyObject *res = PyObject_CallFunction(m->deinit, "()");
			Py_XDECREF(res);
			Py_XDECREF(m->deinit);
		}
	}
	
	list_destroy(modules, 1);
	
	Py_Finalize();

	return 0;
}

/*
 * python_unload()
 *
 * usuwa z pami�ci podany skrypt.
 *
 *  - name - nazwa skryptu
 *
 * 0/-1
 */
int python_unload(const char *name)
{
	list_t l;

	for (l = modules; l; l = l->next) {
		struct module *m = l->data;

		if (strcmp(m->name, name))
			continue;

		gg_debug(GG_DEBUG_MISC, "m->deinit = %p, hmm?\n", m->deinit);
		if (m->deinit) {
			PyObject *res = PyObject_CallFunction(m->deinit, "()");
			Py_XDECREF(res);
			Py_XDECREF(m->deinit);
		}

		list_remove(&modules, m, 1);

		print("generic", "Skrypt zosta� usuni�ty");

		return 0;
	}
	
	print("generic", "Nie znaleziono skryptu");
	
	return -1;
}

/*
 * python_run()
 *
 * uruchamia jednorazowo skrypt pythona.
 *
 * 0/-1
 */
int python_run(const char *filename)
{
	FILE *f = fopen(filename, "r");

	if (f) {
		PyRun_SimpleFile(f, (char*) filename);
		fclose(f);
	} else
		return -1;

	return 0;
}

/*
 * python_get_func()
 *
 * zwraca dan� funkcj� modu�u.
 */
PyObject *python_get_func(PyObject *module, const char *name)
{
	PyObject *result = PyObject_GetAttrString(module, (char*) name);

	if (result && !PyCallable_Check(result)) {
		Py_XDECREF(result);
		result = NULL;
	}

	return result;
}

/*
 * python_load()
 *
 * �aduje skrypt pythona o podanej nazwie z ~/.gg/scripts
 *
 *  - name - nazwa skryptu
 *
 * 0/-1
 */
int python_load(const char *name)
{
	PyObject *module, *init;
	struct module m;
	char *name2;

	if (!name)
		return -1;
	
	if (strchr(name, '/')) {
		char *tmp = saprintf("Skrypt nale�y umie�ci� w katalogu \033[1m%s\033[0m", prepare_path("scripts", 0));
		print("generic", tmp);
		xfree(tmp);
		return -1;
	}

	name2 = xstrdup(name);

	if (strlen(name2) > 3 && !strcasecmp(name2 + strlen(name2) - 3, ".py"))
		name2[strlen(name2) - 3] = 0;

	module = PyImport_ImportModule(name2);

	if (!module) {
		print("generic", "Nie znaleziono skryptu");
		PyErr_Print();
		xfree(name2);
		return -1;
	}

	if ((init = PyObject_GetAttrString(module, "init"))) {
		if (PyCallable_Check(init)) {
			PyObject *result = PyObject_CallFunction(init, "()");

			if (result) {
				int resulti = PyInt_AsLong(result);

				if (!resulti) {
					
				}

				Py_XDECREF(result);
			}
		}

		Py_XDECREF(init);
	}

	memset(&m, 0, sizeof(m));

	m.name = xstrdup(name2);
	m.module = module;
	m.deinit = python_get_func(module, "deinit");
	m.handle_msg = python_get_func(module, "handle_msg");
	m.handle_connect = python_get_func(module, "handle_connect");
	m.handle_disconnect = python_get_func(module, "handle_disconnect");
	m.handle_status = python_get_func(module, "handle_status");

	list_add(&modules, &m, sizeof(m));
	
	xfree(name2);

	return 0;
}

/*
 * python_exec()
 *
 * wykonuje polecenie pythona.
 * 
 *  - command - polecenie
 * 
 * 0/-1
 */
int python_exec(const char *command)
{
	char *tmp = saprintf("import ekg\n%s\n", command);

	PyRun_SimpleString(tmp);
	xfree(tmp);

	return 0;
}

/*
 * python_list()
 *
 * wy�wietla list� za�adowanych skrypt�w.
 *
 * 0/-1
 */
int python_list()
{
	list_t l;

	for (l = modules; l; l = l->next) {
		struct module *m = l->data;

		print("generic", m->name);
	}

	return 0;
}

int python_function(const char *function, const char *arg)
{
	return -1;
}

int python_autorun()
{
	return -1;
}

/*
 * Fledge "Notify Python35" class
 *
 * Copyright (c) 2019 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string>
#include <iostream>

#include <utils.h>
#include <pyruntime.h>
#include "notify_python35.h"

#define SCRIPT_NAME  "notify35"
#define PYTHON_SCRIPT_METHOD_PREFIX "_script_"
#define PYTHON_SCRIPT_FILENAME_EXTENSION ".py"
#define SCRIPT_CONFIG_ITEM_NAME "script"

using namespace std;

/**
 * NotifyPython35 class constructor
 *
 * @param category	The configuration of the delivery plugin
 */
NotifyPython35::NotifyPython35(ConfigCategory *category)
{
	m_enabled = false;
	m_pModule = NULL;
	m_pFunc = NULL;
	m_pythonScript = string("");
	m_failedScript = false;
	m_execCount = 0;

	m_name = category->getName();

	m_logger = Logger::getLogger();

	// Set the enable flag
	if (category->itemExists("enable"))
	{
		m_enabled = category->getValue("enable").compare("true") == 0 ||
			    category->getValue("enable").compare("True") == 0;
	}

	// Check whether we have a Python 3.5 script file to import
	if (category->itemExists(SCRIPT_CONFIG_ITEM_NAME))
	{
		try
		{
			// Get Python script file from "file" attibute of "scipt" item
			m_pythonScript = category->getItemAttribute(SCRIPT_CONFIG_ITEM_NAME,
								    ConfigCategory::FILE_ATTR);
			// Just take file name and remove path
			std::size_t found = m_pythonScript.find_last_of("/");
			if (found != std::string::npos)
			{
				m_pythonScript = m_pythonScript.substr(found + 1);
			}
		}
		catch (ConfigItemAttributeNotFound* e)
		{
			delete e;
		}
		catch (exception* e)
		{
			delete e;
		}
	}

	if (m_pythonScript.empty())
	{
		m_logger->warn("Notification plugin '%s', "
				  "called without a Python 3.5 script. "
				  "Check 'script' item in '%s' configuration. "
				  "Notification plugin has been disabled.",
			 	  PLUGIN_NAME,
				  this->getName().c_str());
	}
}

/**
 * NotifyPython35 class destructor
 */
NotifyPython35::~NotifyPython35()
{
}

/**
 * Configure Python35 plugin:
 *
 * import the Python script file and call
 * script configuration method with current plugin configuration
 *
 * This method must be called while holding the configuration mutex
 *
 * @return	True on success, false on errors.
 */
bool NotifyPython35::configure()
{
	m_failedScript = false;

	// Import script as module
	// NOTE:
	// Script file name is:
	// lowercase(categoryName) + _script_ + methodName + ".py"

	string filterMethod;
	std::size_t found;

	// 1) Get methodName
	found = m_pythonScript.rfind(PYTHON_SCRIPT_METHOD_PREFIX);
	if (found != std::string::npos)
	{
		filterMethod = m_pythonScript.substr(found + strlen(PYTHON_SCRIPT_METHOD_PREFIX));
	}
	// Remove .py from filterMethod
	found = filterMethod.rfind(PYTHON_SCRIPT_FILENAME_EXTENSION);
	if (found != std::string::npos)
	{
		filterMethod.replace(found, strlen(PYTHON_SCRIPT_FILENAME_EXTENSION), "");
	}
	// Remove .py from pythonScript
	found = m_pythonScript.rfind(PYTHON_SCRIPT_FILENAME_EXTENSION);
	if (found != std::string::npos)
	{
		m_pythonScript.replace(found, strlen(PYTHON_SCRIPT_FILENAME_EXTENSION), "");
	}

	m_logger->debug("%s delivery plugin: script='%s', method='%s'",
			   PLUGIN_NAME,
			   m_pythonScript.c_str(),
			   filterMethod.c_str());

	// 2) Import Python script
	// check first method name is empty:
	// disable delivery, cleanup and return true
	// This allows reconfiguration
        if (filterMethod.empty())
	{
		// Force disable
		this->disableDelivery();

		m_pModule = NULL;
		m_pFunc = NULL;

		return true;
	}

	// 2) Import Python script if module object is not set
	if (!m_pModule)
	{
		m_pModule = PyImport_ImportModule(m_pythonScript.c_str());
	}

	// Check whether the Python module has been imported
	if (!m_pModule)
	{
		// Failure
		if (PyErr_Occurred())
		{
			logErrorMessage();
		}
		m_logger->fatal("Notification plugin '%s' (%s), can not import Python 3.5 script "
				   "'%s' from '%s'",
				   PLUGIN_NAME,
				   this->getName().c_str(),
				   m_pythonScript.c_str(),
				   m_scriptsPath.c_str());

		m_failedScript = true;

		return false;
	}

	// Fetch filter method in loaded object
	m_pFunc = PyObject_GetAttrString(m_pModule, filterMethod.c_str());
	if (!PyCallable_Check(m_pFunc))
	{
		// Failure
		if (PyErr_Occurred())
		{
			logErrorMessage();
		}

		m_logger->fatal("Notification plugin %s (%s) error: cannot "
				   "find Python 3.5 method "
				   "'%s' in loaded module '%s.py'",
				   PLUGIN_NAME,
				   this->getName().c_str(),
				   filterMethod.c_str(),
				   m_pythonScript.c_str());
		Py_CLEAR(m_pModule);
		Py_CLEAR(m_pFunc);

		m_failedScript = true;

		return false;
	}

	return true;
}

/**
 * Reconfigure the delivery plugin
 *
 * @param newConfig	The new configuration
 */
bool NotifyPython35::reconfigure(const std::string& newConfig)
{
	m_logger->debug("%s notification 'plugin_reconfigure' called = %s",
			   PLUGIN_NAME,
			   newConfig.c_str());

	ConfigCategory category("new", newConfig);
	string newScript;

	// Configuration change is protected by a lock
	lock_guard<mutex> guard(m_configMutex);

	PyGILState_STATE state = PyGILState_Ensure(); // acquire GIL

	// Get Python script file from "file" attibute of "scipt" item
	if (category.itemExists(SCRIPT_CONFIG_ITEM_NAME))
	{
		try
		{
			// Get Python script file from "file" attibute of "scipt" item
			newScript = category.getItemAttribute(SCRIPT_CONFIG_ITEM_NAME,
								   ConfigCategory::FILE_ATTR);
		        // Just take file name and remove path
			std::size_t found = newScript.find_last_of("/");
			if (found != std::string::npos)
			{
				newScript = newScript.substr(found + 1);

				// Remove .py from pythonScript
				found = newScript.rfind(PYTHON_SCRIPT_FILENAME_EXTENSION);
				if (found != std::string::npos)
				{
					newScript.replace(found, strlen(PYTHON_SCRIPT_FILENAME_EXTENSION), "");
				}
			}
		}
		catch (ConfigItemAttributeNotFound* e)
		{
			delete e;
		}
		catch (exception* e)
		{
			delete e;
		}
	}

	if (newScript.empty())
	{
		m_logger->warn("Notification plugin '%s', "
				  "called without a Python 3.5 script. "
				  "Check 'script' item in '%s' configuration. "
				  "Notification plugin has been disabled.",
				  PLUGIN_NAME,
				  this->getName().c_str());
		// Force disable
		this->disableDelivery();

		PyGILState_Release(state);

		m_failedScript = true;

		return false;
	}

	// Reload module or Import module ?
	if (newScript.compare(m_pythonScript) == 0 && m_pModule)
	{
		m_failedScript = false;
		m_execCount = 0;

		// Reimport module
		PyObject* newModule = PyImport_ReloadModule(m_pModule);
		if (newModule)
		{
			// Cleanup Loaded module
			Py_CLEAR(m_pModule);
			m_pModule = NULL;
			Py_CLEAR(m_pFunc);
			m_pFunc = NULL;

			// Set new name
			m_pythonScript = newScript;

			// Set reloaded module
			m_pModule = newModule;
		}
		else
		{
			// Errors while reloading the Python module
			m_logger->error("%s notification error while reloading "
					   " Python script '%s' in 'plugin_reconfigure'",
					   PLUGIN_NAME,
					   m_pythonScript.c_str());
			logErrorMessage();

			PyGILState_Release(state);

			m_failedScript = true;

			return false;
		}
	}
	else
	{
		m_failedScript = false;
		m_execCount = 0;
		// Import the new module

		// Cleanup Loaded module
		Py_CLEAR(m_pModule);
		m_pModule = NULL;
		Py_CLEAR(m_pFunc);
		m_pFunc = NULL;

		// Set new name
		m_pythonScript = newScript;

		// Import the new module
		PyObject* newModule = PyImport_ImportModule(m_pythonScript.c_str());

		// Set reloaded module
		m_pModule = newModule;
	}

	// Set the enable flag
	if (category.itemExists("enable"))
	{
		m_enabled = category.getValue("enable").compare("true") == 0 ||
			    category.getValue("enable").compare("True") == 0;
	}

	bool ret = this->configure();

	PyGILState_Release(state);

	return ret;
}

/**
 * Call Python 3.5 notification method
 *
 * @param notificationName 	The name of this notification
 * @param triggerReason		Why the notification is being sent
 * @param message		The message to send
 */
bool NotifyPython35::notify(const std::string& deliveryName,
			    const string& notificationName,
			    const string& triggerReason,
			    const string& customMessage)
{
	lock_guard<mutex> guard(m_configMutex);
	bool ret = false;

        if (!m_enabled)
        {
                // Current plugin is not active: just return
                return false;
        }

	if (m_failedScript)
	{
		// Just log once
		if (m_execCount++ >= MAX_ERRORS_COUNT)
		{
			m_logger->warn("The '%s' notification is unable to process data " \
					"as the supplied Python script '%s' has errors.",
					m_name.c_str(),
					m_pythonScript.c_str());
			// Reset counter
			m_execCount = 0;
		}
		return false;
	}

	if (! Py_IsInitialized())
	{
		m_logger->fatal("The Python environment failed to initialize, " \
				"the %s notification plugin is unable to process any data",
				m_name.c_str());
		return false;
	}

	PyGILState_STATE state = PyGILState_Ensure();

	// Save configuration variables and Python objects
	string name = m_name;
	string scriptName = m_pythonScript;
	PyObject* method = m_pFunc;

	// Call Python method passing an object
	PyObject* pReturn = PyObject_CallFunction(method,
						  "s",
						  customMessage.c_str());

	// Check return status
	if (!pReturn)
	{
		// Errors while getting result object
		m_logger->error("Notification plugin '%s' (%s), error in script '%s'",
				   PLUGIN_NAME,
				   name.c_str(),
				   scriptName.c_str());

		// Errors while getting result object
		logErrorMessage();

		// Mark failure to reduce excessive logging
		m_failedScript = true;
	}
	else
	{
		ret = true;
		m_logger->debug("PyObject_CallFunction() succeeded");

		// Remove pReturn object
		Py_CLEAR(pReturn);
	}

	m_logger->debug("Notification '%s' 'plugin_delivery' " \
			   "called, return = %d",
			   this->getName().c_str(),
			   ret);

	PyGILState_Release(state);

	return ret;
}

/**
 * Shutdown the Python35 notification plugin
 */
void NotifyPython35::shutdown()
{
	PyGILState_STATE state = PyGILState_Ensure();

	// Decrement pModule reference count
	Py_CLEAR(m_pModule);

	// Decrement pFunc reference count
	Py_CLEAR(m_pFunc);

	// Interpreter is still running, just release the GIL
	PyGILState_Release(state); // release GIL
}

bool NotifyPython35::init()
{
	// Embedded Python 3.5 program name
	wchar_t *programName = Py_DecodeLocale(m_name.c_str(), NULL);
	Py_SetProgramName(programName);
	PyMem_RawFree(programName);

	// Embedded Python 3.5 initialisation
	PythonRuntime::getPythonRuntime();

	PyGILState_STATE state = PyGILState_Ensure(); // acquire GIL

	// Add scripts dir: pass Fledge Data dir
	this->setScriptsPath(getDataDir());

	// Set Python path for embedded Python 3.5
	// Get current sys.path. borrowed reference
	PyObject* sysPath = PySys_GetObject((char *)string("path").c_str());
	// Add Fledge python scripts path
	PyObject* pPath = PyUnicode_DecodeFSDefault((char *)this->getScriptsPath().c_str());
	PyList_Insert(sysPath, 0, pPath);
	// Remove temp object
	Py_CLEAR(pPath);

	// Check first we have a Python script to load
	if (this->getScriptName().empty())
	{
		// Force disable
		this->disableDelivery();
	}

	// Configure plugin
	this->lock();
	bool ret = this->configure();
	this->unlock();

	PyGILState_Release(state); // release GIL

	return ret;
}

/**
 * Log current Python 3.5 error message
 *
 */
void NotifyPython35::logErrorMessage()
{
	if (PyErr_Occurred())
	{
		// Get error message
		PyObject *ptype, *pvalue, *ptraceback;
		PyErr_Fetch(&ptype, &pvalue, &ptraceback);
		PyErr_NormalizeException(&ptype,&pvalue,&ptraceback);

		char *msg, *file, *text;
		int line, offset;

		int res = PyArg_ParseTuple(pvalue,"s(siis)",&msg,&file,&line,&offset,&text);

		PyObject *line_no = PyObject_GetAttrString(pvalue,"lineno");
		PyObject *line_no_str = PyObject_Str(line_no);
		PyObject *line_no_unicode = PyUnicode_AsEncodedString(line_no_str,"utf-8", "Error");
		char *actual_line_no = PyBytes_AsString(line_no_unicode);  // Line number

		PyObject *ptext = PyObject_GetAttrString(pvalue,"text");
		PyObject *ptext_str = PyObject_Str(ptext);
		PyObject *ptext_no_unicode = PyUnicode_AsEncodedString(ptext_str,"utf-8", "Error");
		char *error_line = PyBytes_AsString(ptext_no_unicode);  // Line in error

		// Remove the trailing newline from the string
		char *newline = rindex(error_line,  '\n');
		if (newline)
		{
			*newline = '\0';
		}

		// Not managed to find a way to get the actual error message from Python
		// so use the string representation of the Error class and tidy it up, e.g.
		// SyntaxError('invalid syntax', ('/tmp/scripts/test_addition_script_script.py', 9, 1, ')\n')
		PyObject *pstr = PyObject_Repr(pvalue);
		PyObject *perr = PyUnicode_AsEncodedString(pstr, "utf-8", "Error");
		char *err_msg = PyBytes_AsString(perr);
		char *end = index(err_msg, ',');
		if (end)
		{
			*end = '\0';
		}
		end = index(err_msg, '(');
		if (end)
		{
			*end = ' ';
		}

		if (error_line == NULL ||
		    actual_line_no == NULL ||
		    strcmp(error_line, "<NULL>") == 0 ||
		    strcmp(actual_line_no, "<NULL>") == 0)
		{
			m_logger->error("Python error: %s in supplied script '%s'",
					err_msg,
					m_pythonScript.c_str());
		}
		else
		{
			m_logger->error("Python error: %s in %s at line %s of supplied script '%s'",
					err_msg,
					error_line,
					actual_line_no,
					m_pythonScript.c_str());
		}

		// Reset error
		PyErr_Clear();

		// Remove objects
		Py_CLEAR(line_no);
		Py_CLEAR(line_no_str);
		Py_CLEAR(line_no_unicode);
		Py_CLEAR(ptext);
		Py_CLEAR(ptext_str);
		Py_CLEAR(ptext_no_unicode);
		Py_CLEAR(pstr);
		Py_CLEAR(perr);

		Py_CLEAR(ptype);
		Py_CLEAR(pvalue);
		Py_CLEAR(ptraceback);
	}
}

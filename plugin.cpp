/*
 * FogLAMP "Python 3.5" notification plugin.
 *
 * Copyright (c) 2018 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Amandeep Singh Arora
 */

#include <plugin_api.h>
#include <config_category.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string>
#include <iostream>
#include <filter_plugin.h>
#include <filter.h>
#include <reading_set.h>
#include <utils.h>

#include <Python.h>

// Relative path to FOGLAMP_DATA
#define PYTHON_FILTERS_PATH "/scripts"
#define PLUGIN_NAME "python35"
#define PYTHON_SCRIPT_METHOD_PREFIX "_script_"
#define PYTHON_SCRIPT_FILENAME_EXTENSION ".py"
#define SCRIPT_CONFIG_ITEM_NAME "script"

/**
 * The Python 3.5 script module to load is set in
 * 'script' config item and it doesn't need the trailing .py
 *
 * Example:
 * if filename is 'notify_alert.py', just set 'notify_alert'
 * via FogLAMP configuration manager
 *
 * Note:
 * Python 3.5 filter code needs only one method which accepts
 * a message string indicating notification trigger and processes 
 * the same as required.
 */

// Filter configuration method
//#define DEFAULT_FILTER_CONFIG_METHOD "set_filter_config"

#define SCRIPT_NAME  "notify35"

// Filter default configuration
#define DEFAULT_CONFIG "{\"plugin\" : { \"description\" : \"Python 3.5 notification plugin\", " \
                       		"\"type\" : \"string\", " \
				"\"default\" : \"" PLUGIN_NAME "\" }, " \
			 "\"enable\": {\"description\": \"A switch that can be used to enable or disable execution of " \
					 "the Python 3.5 notification plugin.\", " \
				"\"type\": \"boolean\", " \
				"\"default\": \"false\" }, " \
			"\"config\" : {\"description\" : \"Python 3.5 filter configuration.\", " \
				"\"type\" : \"JSON\", " \
				"\"default\" : {}}, " \
			"\"script\" : {\"description\" : \"Python 3.5 module to load.\", " \
				"\"type\": \"script\", " \
				"   \"default\": \"" SCRIPT_NAME "\"} }"
using namespace std;

typedef struct
{
	FogLampFilter *handle;
	PyObject* pModule; // Python 3.5 loaded filter module handle
	PyObject* pFunc; // Python 3.5 callable method handle
	string pythonScript; // Python 3.5 script name
} PLUGIN_INFO;

void logErrorMessage();

/**
 * The Filter plugin interface
 */
extern "C" {
/**
 * The plugin information structure
 */
static PLUGIN_INFORMATION info = {
        PLUGIN_NAME,                              // Name
        "1.0.0",                                  // Version
        0,                                        // Flags
        PLUGIN_TYPE_NOTIFICATION_DELIVERY,        // Type
        "1.0.0",                                  // Interface version
        DEFAULT_CONFIG	                          // Default plugin configuration
};

/**
 * Return the information about this plugin
 */
PLUGIN_INFORMATION *plugin_info()
{
	return &info;
}

/**
 * Initialise the plugin, called to get the plugin handle and setup the
 * output handle that will be passed to the output stream. The output stream
 * is merely a function pointer that is called with the output handle and
 * the new set of readings generated by the plugin.
 *     (*output)(outHandle, readings);
 * Note that the plugin may not call the output stream if the result of
 * the filtering is that no readings are to be sent onwards in the chain.
 * This allows the plugin to discard data or to buffer it for aggregation
 * with data that follows in subsequent calls
 *
 * @param config	The configuration category for the filter
 * @param outHandle	A handle that will be passed to the output stream
 * @param output	The output stream (function pointer) to which data is passed
 * @return		An opaque handle that is used in all subsequent calls to the plugin
 */
PLUGIN_HANDLE plugin_init(ConfigCategory* config,
			  OUTPUT_HANDLE *outHandle,
			  OUTPUT_STREAM output)
{
	FogLampFilter* handle = new FogLampFilter(PLUGIN_NAME,
						  *config,
						  outHandle,
						  output);
	PLUGIN_INFO *info = new PLUGIN_INFO;
	info->handle = handle;
	info->pythonScript = string("");

	// Check whether we have a Python 3.5 script file to import
	if (handle->getConfig().itemExists(SCRIPT_CONFIG_ITEM_NAME))
	{
		try
		{
			// Get Python script file from "file" attibute of "scipt" item
			info->pythonScript = handle->getConfig().getItemAttribute(SCRIPT_CONFIG_ITEM_NAME,
									    ConfigCategory::FILE_ATTR);
		        // Just take file name and remove path
			std::size_t found = info->pythonScript.find_last_of("/");
			info->pythonScript = info->pythonScript.substr(found + 1);
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

	if (info->pythonScript.empty())
	{
		// Do nothing
		Logger::getLogger()->warn("Notification plugin '%s', "
					  "called without a Python 3.5 script. "
					  "Check 'script' item in '%s' configuration. "
					  "Notification plugin has been disabled.",
					  PLUGIN_NAME,
					  handle->getConfig().getName().c_str());

		// Force disable
		handle->disableFilter();

		// Return filter handle
		return (PLUGIN_HANDLE)info;
	}
		
	// Embedded Python 3.5 program name
	wchar_t *programName = Py_DecodeLocale(config->getName().c_str(), NULL);
	Py_SetProgramName(programName);
	PyMem_RawFree(programName);
	// Embedded Python 3.5 initialisation
	Py_Initialize();

	// Get FogLAMP Data dir
	string filtersPath = getDataDir();
	// Add filters dir
	filtersPath += PYTHON_FILTERS_PATH;

	// Set Python path for embedded Python 3.5
	// Get current sys.path. borrowed reference
	PyObject* sysPath = PySys_GetObject((char *)string("path").c_str());
	// Add FogLAMP python filters path
	PyObject* pPath = PyUnicode_DecodeFSDefault((char *)filtersPath.c_str());
	PyList_Insert(sysPath, 0, pPath);
	// Remove temp object
	Py_CLEAR(pPath);

	// Import script as module
	// NOTE:
	// Script file name is:
	// lowercase(categoryName) + _script_ + methodName + ".py"

	// 1) Get methodName
	std::size_t found = info->pythonScript.rfind(PYTHON_SCRIPT_METHOD_PREFIX);
	string filterMethod = info->pythonScript.substr(found + strlen(PYTHON_SCRIPT_METHOD_PREFIX));
	// Remove .py from filterMethod
	found = filterMethod.rfind(PYTHON_SCRIPT_FILENAME_EXTENSION);
	filterMethod.replace(found,
			     strlen(PYTHON_SCRIPT_FILENAME_EXTENSION),
			     "");
	// Remove .py from pythonScript
	found = info->pythonScript.rfind(PYTHON_SCRIPT_FILENAME_EXTENSION);
	info->pythonScript.replace(found,
			     strlen(PYTHON_SCRIPT_FILENAME_EXTENSION),
			     "");

	// 2) Import Python script
	info->pModule = PyImport_ImportModule(info->pythonScript.c_str());

	// Check whether the Python module has been imported
	if (!info->pModule)
	{
		// Failure
		if (PyErr_Occurred())
		{
			logErrorMessage();
		}
		Logger::getLogger()->fatal("Notification plugin '%s' (%s), cannot import Python 3.5 script "
					   "'%s' from '%s'",
					   PLUGIN_NAME,
					   handle->getConfig().getName().c_str(),
					   info->pythonScript.c_str(),
					   filtersPath.c_str());

		// This will abort the filter pipeline set up
		return NULL;
	}

	// Fetch filter method in loaded object
	info->pFunc = PyObject_GetAttrString(info->pModule, filterMethod.c_str());

	if (!PyCallable_Check(info->pFunc))
	{
		// Failure
		if (PyErr_Occurred())
		{
			logErrorMessage();
		}

		Logger::getLogger()->fatal("Notification plugin %s (%s) error: cannot find Python 3.5 method "
					   "'%s' in loaded module '%s.py'",
					   PLUGIN_NAME,
					   handle->getConfig().getName().c_str(),
					   info->pythonScript.c_str(),
					   info->pythonScript.c_str());
		Py_CLEAR(info->pModule);
		Py_CLEAR(info->pFunc);

		// This will abort the filter pipeline set up
		return NULL;
	}
	
	// Return filter handle
	return (PLUGIN_HANDLE)info;
}

/**
 * Deliver received notification data
 *
 * @param handle		The plugin handle returned from plugin_init
 * @param deliveryName		The delivery category name
 * @param notificationName	The notification name
 * @param triggerReason		The trigger reason for notification
 * @param message		The message from notification
 */
bool plugin_deliver(PLUGIN_HANDLE handle,
                    const std::string& deliveryName,
                    const std::string& notificationName,
                    const std::string& triggerReason,
                    const std::string& message)
{
	PLUGIN_INFO *info = (PLUGIN_INFO *) handle;
	FogLampFilter* filter = info->handle;

	if (!filter->isEnabled())
	{
		// Current filter is not active: just return
		return false;
	}
	
	// Call Python method passing an object
	PyObject* pReturn = PyObject_CallFunction(info->pFunc,
						  "s",
						  message.c_str());

	// Check return status
	if (!pReturn)
	{
		// Errors while getting result object
		Logger::getLogger()->error("Notification plugin '%s' (%s), script '%s', "
					   "filter error, action: %s",
					   PLUGIN_NAME,
					   filter->getConfig().getName().c_str(),
					   info->pythonScript.c_str(),
					   "pass unfiltered data onwards");

		// Errors while getting result object
		logErrorMessage();
	}
	else
	{
		Logger::getLogger()->info("PyObject_CallFunction() succeeded");
		
		// Remove pReturn object
		Py_CLEAR(pReturn);
	}
}

/**
 * Call the shutdown method in the plugin
 */
void plugin_shutdown(PLUGIN_HANDLE *handle)
{
	PLUGIN_INFO *info = (PLUGIN_INFO *) handle;
	FogLampFilter* filter = info->handle;

	// Decrement pModule reference count
	Py_CLEAR(info->pModule);
	// Decrement pFunc reference count
	Py_CLEAR(info->pFunc);

	// Cleanup Python 3.5
	Py_Finalize();
	delete filter;
	delete handle;
}

// End of extern "C"
};

/**
 * Log current Python 3.5 error message
 *
 */
void logErrorMessage()
{
	//Get error message
	PyObject *pType, *pValue, *pTraceback;
	PyErr_Fetch(&pType, &pValue, &pTraceback);

	// NOTE from :
	// https://docs.python.org/2/c-api/exceptions.html
	//
	// The value and traceback object may be NULL
	// even when the type object is not.	
	const char* pErrorMessage = pValue ?
				    PyBytes_AsString(pValue) :
				    "no error description.";

	Logger::getLogger()->fatal("Notification plugin '%s', Error '%s'",
				   PLUGIN_NAME,
				   pErrorMessage ?
				   pErrorMessage :
				   "no description");

	// Reset error
	PyErr_Clear();

	// Remove references
	Py_CLEAR(pType);
	Py_CLEAR(pValue);
	Py_CLEAR(pTraceback);
}


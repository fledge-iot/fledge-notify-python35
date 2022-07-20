#ifndef _PYTHON35_FILTER_H
#define _PYTHON35_FILTER_H
/*
 * Fledge "Python 3.5" filter class.
 *
 * Copyright (c) 2019 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */

#include <mutex>

#include <filter_plugin.h>
#include <filter.h>

#include <Python.h>

#define PLUGIN_NAME "python35"

// Relative path to FLEDGE_DATA
#define PYTHON_FILTERS_PATH "/scripts"

// Maximum number of error messages to skip in notify method
// When max is reached a log messages will be added and counter is rest
#define MAX_ERRORS_COUNT 100

/**
 * NotifyPython35 handles plugin configuration and Python objects
 */
class NotifyPython35
{
	public:
		NotifyPython35(ConfigCategory *category);

		~NotifyPython35();

		bool	notify(const std::string& deliveryName,
				const std::string& notificationName,
				const std::string& triggerReason,
				const std::string& message);

		const std::string& getName() { return m_name; };
		// Set the additional path for Python3.5 Fledge scripts
		void	setScriptsPath(const std::string& dataDir)
		{
			// Set Fledge dataDir + filters dir
			m_scriptsPath = dataDir + PYTHON_FILTERS_PATH;
		}

		const std::string&
			getScriptsPath() const { return m_scriptsPath; };
		const std::string&
			getScriptName() const { return m_pythonScript; };
		void	disableDelivery() { m_enabled = false; };
		bool	configure();
		bool	reconfigure(const std::string& newConfig);
		bool	isEnabled() const { return m_enabled; };
		void	lock() { m_configMutex.lock(); };
		void	unlock() { m_configMutex.unlock(); };
		void	logErrorMessage();
		void	shutdown();
		bool	init();
		
	private:
		// Python 3.5 loaded filter module handle
		PyObject*	m_pModule;
		// Python 3.5 callable method handle
		PyObject*	m_pFunc;
		// Plugin is enabled
		bool		m_enabled;
		// Python 3.5  script name
		std::string	m_pythonScript;
		// Scripts path
		std::string	m_scriptsPath;
		// Plugin category name
		std::string	m_name;
		// Configuration lock
		std::mutex	m_configMutex;
		// Logger
		Logger		*m_logger;
		bool		m_failedScript;
		int		m_execCount;
};
#endif

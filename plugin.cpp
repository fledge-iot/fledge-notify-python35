/*
 * Fledge "Python 3.5" notification plugin.
 *
 * Copyright (c) 2018 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Amandeep Singh Arora, Massimiliano Pinto
 */

#include <plugin_api.h>
#include <config_category.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string>
#include <iostream>
#include <reading_set.h>
#include <utils.h>
#include <version.h>
#include <pyruntime.h>
#include "notify_python35.h"

/**
 * The Python 3.5 script module to load is set in
 * 'script' config item and it doesn't need the trailing .py
 *
 * Example:
 * if filename is 'notify_alert.py', just set 'notify_alert'
 * via Fledge configuration manager
 *
 * Note:
 * Python 3.5 delivery plugin code needs only one method which accepts
 * a message string indicating notification trigger and processes 
 * the same as required.
 */


// Delivery plugin default configuration
static const char *default_config = QUOTE({
	"plugin" : {
	       	"description" : "Python 3.5 notification plugin",
		"type" : "string",
		"default" : PLUGIN_NAME,
		"readonly" : "true"
		},
	"enable": {
		"description": "A switch that can be used to enable or disable execution of the Python 3.5 notification plugin.", 
		"type": "boolean", 
		"displayName" : "Enabled",
		"order" : "3", 
		"default": "false"
		}, 
	"config" : {
		"description" : "Python 3.5 configuration.", 
		"type" : "JSON", 
		"displayName" : "Configuration",
		"order" : "2",
		"default" : "{}"
		}, 
	"script" : {
		"description" : "Python 3.5 script to load.", 
		"type": "script", 
		"displayName" : "Python script",
		"order" : "1",
		"default": ""
		}
	});

using namespace std;

/**
 * The Delivery plugin interface
 */
extern "C" {
/**
 * The plugin information structure
 */
static PLUGIN_INFORMATION info = {
        PLUGIN_NAME,                              // Name
        VERSION,                                  // Version
        0,                                        // Flags
        PLUGIN_TYPE_NOTIFICATION_DELIVERY,        // Type
        "1.0.0",                                  // Interface version
        default_config	                          // Default plugin configuration
};

/**
 * Return the information about this plugin
 */
PLUGIN_INFORMATION *plugin_info()
{
	return &info;
}

/**
 * Initialise the plugin, called to get the plugin handle.
 *
 * @param config	The configuration category for the delivery plugin
 * @return		An opaque handle that is used in all subsequent calls to the plugin
 */
PLUGIN_HANDLE plugin_init(ConfigCategory* config)
{
	// Instantiate plugin class
	NotifyPython35* notify = new NotifyPython35(config);

	// Initialise plugin and Python interpreter
	bool ret = notify->init();

	if (!ret)
	{
		// Free object
		delete notify;
		notify = NULL;
	}

	// Return plugin handle: NULL will abort the plugin init
	return (PLUGIN_HANDLE)notify;
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
	NotifyPython35* notify = (NotifyPython35 *) handle;

	// Protect against reconfiguration
	notify->lock();
	bool enabled = notify->isEnabled();
	notify->unlock();

	if (!enabled)
	{
		return false;
	}

	// Call notify method
	return notify->notify(deliveryName,
			      notificationName,
			      triggerReason,
			      message);
}

/**
 * Call the shutdown method in the plugin
 */
void plugin_shutdown(PLUGIN_HANDLE *handle)
{
	NotifyPython35* notify = (NotifyPython35 *) handle;

	// Plugin cleanup
	notify->shutdown();

	// Cleanup memory
	delete notify;
}

/**
 * Reconfigure the plugin
 */
void plugin_reconfigure(PLUGIN_HANDLE *handle,
			string& newConfig)
{
	NotifyPython35* notify = (NotifyPython35 *)handle;
	
	notify->reconfigure(newConfig);
}

// End of extern "C"
};

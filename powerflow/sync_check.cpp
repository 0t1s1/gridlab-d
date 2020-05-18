/** $Id: sync_check

    Implements sychronization check functionality for switches to close
    when two grids are within parameters

	Copyright (C) 2020 Battelle Memorial Institute
**/

#include "sync_check.h"

#include <iostream>
#include <cmath>

using namespace std;

/* UTIL MACROS */
#define RAD_TO_DEG(rad) rad / M_PI * 180
#define STR(s) #s

//////////////////////////////////////////////////////////////////////////
// sync_check CLASS FUNCTIONS
//////////////////////////////////////////////////////////////////////////
CLASS *sync_check::oclass = NULL;
CLASS *sync_check::pclass = NULL;

sync_check::sync_check(MODULE *mod) : powerflow_object(mod)
{
	if (oclass == NULL)
	{
		pclass = powerflow_object::oclass;
		oclass = gl_register_class(mod, "sync_check", sizeof(sync_check), PC_PRETOPDOWN | PC_BOTTOMUP | PC_POSTTOPDOWN | PC_AUTOLOCK);
		if (oclass == NULL)
			GL_THROW("unable to register object class implemented by %s", __FILE__);
		if (gl_publish_variable(oclass,
								PT_bool, "armed", PADDR(sc_enabled_flag), PT_DESCRIPTION, "Flag to arm the synchronization close",
								PT_double, "frequency_tolerance[Hz]", PADDR(frequency_tolerance_hz), PT_DESCRIPTION, "The user-specified tolerance for checking the frequency metric",
								PT_double, "voltage_tolerance[pu]", PADDR(voltage_tolerance_pu), PT_DESCRIPTION, "voltage_tolerance",
								PT_double, "metrics_period[s]", PADDR(metrics_period_sec), PT_DESCRIPTION, "The user-defined period when both metrics are satisfied",
								PT_enumeration, "volt_compare_mode", PADDR(volt_compare_mode), PT_DESCRIPTION, "Determines which voltage difference calculation approach is used",
									PT_KEYWORD, "MAG_DIFF", (enumeration)MAG_DIFF,
									PT_KEYWORD, "SEP_DIFF", (enumeration)SEP_DIFF,
								PT_double, "voltage_magnitude_tolerance[pu]", PADDR(voltage_magnitude_tolerance_pu), PT_DESCRIPTION, "The user-specified tolerance in per unit for the difference in voltage magnitudes for checking the voltage metric. Used only by the SEP_DIFF mode of volt_compare_mode.",
								PT_double, "voltage_angle_tolerance[deg]", PADDR(voltage_angle_tolerance_deg), PT_DESCRIPTION, "The user-specified tolerance in degrees for the difference in voltage angles for checking the voltage metric. Used only by the SEP_DIFF mode of volt_compare_mode.",
								PT_double, "delta_trigger_mult", PADDR(delta_trigger_mult), PT_DESCRIPTION, "User-specified multiplier against voltage and frequency tolerances to trigger/maintain deltamode",
								NULL) < 1)
			GL_THROW("unable to publish properties in %s", __FILE__);

		if (gl_publish_function(oclass, "interupdate_pwr_object", (FUNCTIONADDR)interupdate_sync_check) == NULL)
			GL_THROW("Unable to publish sync_check deltamode function");
	}
}

int sync_check::isa(char *classname)
{
	return strcmp(classname, "sync_check") == 0;
}

int sync_check::create(void)
{
	int result = powerflow_object::create();
	init_vars();

	return result;
}

int sync_check::init(OBJECT *parent)
{
	int retval = powerflow_object::init(parent);

	data_sanity_check(parent);
	init_norm_values(parent);
	init_sensors(parent);
	reg_deltamode_check();

	return retval;
}

TIMESTAMP sync_check::presync(TIMESTAMP t0)
{
	TIMESTAMP tret = powerflow_object::presync(t0);

	reg_deltamode();

	return tret;
}

TIMESTAMP sync_check::sync(TIMESTAMP t0)
{
	OBJECT *obj = OBJECTHDR(this);
	TIMESTAMP tret = powerflow_object::sync(t0);

	return tret;
}

TIMESTAMP sync_check::postsync(TIMESTAMP t0)
{
	OBJECT *obj = OBJECTHDR(this);
	TIMESTAMP tret = powerflow_object::postsync(t0);

	//Code to check if we need a deltamode call
	if (next_trigger_update_time <= t0)
	{
		//Reset accumulator timer, while we're in here (just because)
		t_sat = 0.0;

		//Update the timing tracker - set it one second up (just because)
		next_trigger_update_time = t0 + 1;

		if (sc_enabled_flag == true)
		{
			//Update measurements and check
			update_measurements();
			check_metrics(true);

			//Check if we were flagged
			if (deltamode_trigger_keep_flag == true)
			{
				//Request deltamode
				schedule_deltamode_start(t0);

				//Reset our flag, out of paranoia
				deltamode_check_return_val = SM_EVENT;

				//Force a reiteration, just in case
				tret = t0;
			}
			//Default else - don't trigger anything
		}
		else	//not armed, so no need to check things
		{
			//Set variables/flags, just in case something else pulls us into deltamode (paranoia set)
			deltamode_trigger_keep_flag = false;
			deltamode_check_return_val = SM_EVENT;
		}
	}

	return tret;
}

//==Funcs
void sync_check::init_sensors(OBJECT *par)
{
	// Map and pull the phases
	temp_property_pointer = new gld_property(par, "phases");

	// Validate
	if ((temp_property_pointer->is_valid() != true) || (temp_property_pointer->is_set() != true))
	{
		GL_THROW("Unable to map phases property - ensure the parent is a switch");
		/*  TROUBLESHOOT
		While attempting to map the phases property from the parent object, an error was encountered.
		Please check and make sure your parent object is a switch inside the powerflow module and try
		again. If the error persists, please submit your code and a bug report via the issue tracker.
		*/
	}

	// Pull the phase information
	swt_phases = temp_property_pointer->get_set();

	// Clear the temporary pointer
	delete temp_property_pointer;

	// Check the phases
	swt_ph_A_flag = ((phases & PHASE_A) == PHASE_A); // Phase A
	swt_ph_B_flag = ((phases & PHASE_B) == PHASE_B); // Phase B
	swt_ph_C_flag = ((phases & PHASE_C) == PHASE_C); // Phase C

	/* Get properties for measurements */
	OBJECT *obj = OBJECTHDR(this);

	/* Get the 'from' node freq */
	prop_fm_node_freq = new gld_property(swt_fm_node, "measured_frequency");

	// Double check the validity of the 'from' node frequency property
	if ((prop_fm_node_freq->is_valid() != true) || (prop_fm_node_freq->is_double() != true))
	{
		GL_THROW("sync_check:%d %s failed to map the frequency property of the 'from' node of its parent switch_object.",
				 obj->id, (obj->name ? obj->name : "Unnamed"));
		/*  TROUBLESHOOT
		While attempting to map the frequency property of the 'from' node of its parent switch_object, an error occurred.  Please try again.
		If the error persists, please submit your GLM and a bug report to the ticketing system.
		*/
	}

	/* Get the 'to' node freq */
	prop_to_node_freq = new gld_property(swt_to_node, "measured_frequency");

	// Double check the validity of the 'to' node frequency property
	if ((prop_to_node_freq->is_valid() != true) || (prop_to_node_freq->is_double() != true))
	{
		GL_THROW("sync_check:%d %s failed to map the frequency property of the 'to' node of its parent switch_object.",
				 obj->id, (obj->name ? obj->name : "Unnamed"));
		/*  TROUBLESHOOT
		While attempting to map the frequency property of the 'to' node of its parent switch_object, an error occurred.  Please try again.
		If the error persists, please submit your GLM and a bug report to the ticketing system.
		*/
	}

	/* Get the 'from' node volt phasors */
	prop_fm_node_volt_A = new gld_property(swt_fm_node, "voltage_A");

	// Double check the validity of the 'from' node voltage A property
	if ((prop_fm_node_volt_A->is_valid() != true) || (prop_fm_node_volt_A->is_complex() != true))
	{
		GL_THROW("sync_check:%d %s failed to map the voltage_A property of the 'from' node of its parent switch_object.",
				 obj->id, (obj->name ? obj->name : "Unnamed"));
		/*  TROUBLESHOOT
		While attempting to map the voltage_A property of the 'from' node of its parent switch_object, an error occurred.  Please try again.
		If the error persists, please submit your GLM and a bug report to the ticketing system.
		*/
	}

	prop_fm_node_volt_B = new gld_property(swt_fm_node, "voltage_B");

	// Double check the validity of the 'from' node voltage B property
	if ((prop_fm_node_volt_B->is_valid() != true) || (prop_fm_node_volt_B->is_complex() != true))
	{
		GL_THROW("sync_check:%d %s failed to map the voltage_B property of the 'from' node of its parent switch_object.",
				 obj->id, (obj->name ? obj->name : "Unnamed"));
		/*  TROUBLESHOOT
		While attempting to map the voltage_B property of the 'from' node of its parent switch_object, an error occurred.  Please try again.
		If the error persists, please submit your GLM and a bug report to the ticketing system.
		*/
	}

	prop_fm_node_volt_C = new gld_property(swt_fm_node, "voltage_C");

	// Double check the validity of the 'from' node voltage C property
	if ((prop_fm_node_volt_C->is_valid() != true) || (prop_fm_node_volt_C->is_complex() != true))
	{
		GL_THROW("sync_check:%d %s failed to map the voltage_C property of the 'from' node of its parent switch_object.",
				 obj->id, (obj->name ? obj->name : "Unnamed"));
		/*  TROUBLESHOOT
		While attempting to map the voltage_C property of the 'from' node of its parent switch_object, an error occurred.  Please try again.
		If the error persists, please submit your GLM and a bug report to the ticketing system.
		*/
	}

	/* Get the 'to' node volt phasors */
	prop_to_node_volt_A = new gld_property(swt_to_node, "voltage_A");

	// Double check the validity of the 'to' node voltage A property
	if ((prop_to_node_volt_A->is_valid() != true) || (prop_to_node_volt_A->is_complex() != true))
	{
		GL_THROW("sync_check:%d %s failed to map the voltage_A property of the 'to' node of its parent switch_object.",
				 obj->id, (obj->name ? obj->name : "Unnamed"));
		/*  TROUBLESHOOT
		While attempting to map the voltage_A property of the 'to' node of its parent switch_object, an error occurred.  Please try again.
		If the error persists, please submit your GLM and a bug report to the ticketing system.
		*/
	}

	prop_to_node_volt_B = new gld_property(swt_to_node, "voltage_B");

	// Double check the validity of the 'to' node voltage B property
	if ((prop_to_node_volt_B->is_valid() != true) || (prop_to_node_volt_B->is_complex() != true))
	{
		GL_THROW("sync_check:%d %s failed to map the voltage_B property of the 'to' node of its parent switch_object.",
				 obj->id, (obj->name ? obj->name : "Unnamed"));
		/*  TROUBLESHOOT
		While attempting to map the voltage_B property of the 'to' node of its parent switch_object, an error occurred.  Please try again.
		If the error persists, please submit your GLM and a bug report to the ticketing system.
		*/
	}

	prop_to_node_volt_C = new gld_property(swt_to_node, "voltage_C");

	// Double check the validity of the 'to' node voltage C property
	if ((prop_to_node_volt_C->is_valid() != true) || (prop_to_node_volt_C->is_complex() != true))
	{
		GL_THROW("sync_check:%d %s failed to map the voltage_C property of the 'to' node of its parent switch_object.",
				 obj->id, (obj->name ? obj->name : "Unnamed"));
		/*  TROUBLESHOOT
		While attempting to map the voltage_C property of the 'to' node of its parent switch_object, an error occurred.  Please try again.
		If the error persists, please submit your GLM and a bug report to the ticketing system.
		*/
	}
}

void sync_check::init_vars()
{
	OBJECT *obj = OBJECTHDR(this);
	double temp_freq_val;

	/* Default - MAG_DIFF Mode */
	volt_compare_mode = MAG_DIFF;

	/* Settings for SEP_DIFF Mode */
	voltage_magnitude_tolerance_pu = 1e-2;
	voltage_angle_tolerance_deg = 5;

	/* init member with default values */
	reg_dm_flag = false;
	deltamode_inclusive = false; //By default, don't be included in deltamode simulations

	metrics_flag = false;
	t_sat = 0;

	temp_property_pointer = NULL;

	/* init some published properties that have the default value */
	sc_enabled_flag = false; //Unarmed

	/* init measurements, gld objs, & Nominal Values*/
	volt_norm = 0;

	swt_fm_node = NULL;
	swt_to_node = NULL;

	swt_fm_node_freq = 0;
	swt_to_node_freq = 0;

	prop_fm_node_freq = NULL;
	prop_to_node_freq = NULL;

	swt_fm_volt_A = complex(0, 0);
	swt_fm_volt_B = complex(0, 0);
	swt_fm_volt_C = complex(0, 0);

	prop_fm_node_volt_A = NULL;
	prop_fm_node_volt_B = NULL;
	prop_fm_node_volt_C = NULL;

	swt_to_volt_A = complex(0, 0);
	swt_to_volt_B = complex(0, 0);
	swt_to_volt_C = complex(0, 0);

	prop_to_node_volt_A = NULL;
	prop_to_node_volt_B = NULL;
	prop_to_node_volt_C = NULL;

	swt_prop_status = NULL;

	swt_phases = 0;
	swt_ph_A_flag = false;
	swt_ph_B_flag = false;
	swt_ph_C_flag = false;

	//Get frequency to populate default - powerflow global has been set by now
	/* Get the nominal frequency property */
	temp_property_pointer = new gld_property("powerflow::nominal_frequency");

	// Double check the validity of the nominal frequency property
	if ((temp_property_pointer->is_valid() != true) || (temp_property_pointer->is_double() != true))
	{
		GL_THROW("sync_check:%d %s failed to map the nominal_frequency property", obj->id, (obj->name ? obj->name : "Unnamed"));
		/*  TROUBLESHOOT
		While attempting to map the nominal_frequency property, an error occurred.  Please try again.
		If the error persists, please submit your GLM and a bug report to the ticketing system.
		*/
	}

	// Get the value of nominal frequency from this property
	temp_freq_val = temp_property_pointer->get_double();

	// Clean the temporary property pointer
	delete temp_property_pointer;

	//Defaults - mostly to cut down on messages
	frequency_tolerance_hz = 0.01 * temp_freq_val; // i.e., 1%
	voltage_tolerance_pu = 1e-2;  // i.e., 1%
	metrics_period_sec = 1.2;

	//Initialize deltmode trigger variables (will populate most later)
	delta_trigger_mult = 2.0;	//Defaults to 2x the bands
	frequency_tolerance_hz_deltamode_trig = 0.0;
	voltage_tolerance_pu_deltamode_trig = 0.0;
	voltage_magnitude_tolerance_pu_deltamode_trig = 0.0;
	voltage_angle_tolerance_deg_deltamode_trig = 0.0;

	deltamode_trigger_keep_flag = false;
	deltamode_check_return_val = SM_EVENT;	//By default, just want event-driven
}

void sync_check::data_sanity_check(OBJECT *par)
{
	OBJECT *obj = OBJECTHDR(this);
	double temp_freq_val;

	// Check if the parent is a switch_object object
	if (par == NULL)
	{
		GL_THROW("sync_check:%d %s the parent property must be specified!",
				 obj->id, (obj->name ? obj->name : "Unnamed"));
		/*  TROUBLESHOOT
		While checking the parent swtich_object, an error occurred.  Please try again.
		If the error persists, please submit your GLM and a bug report to the ticketing system.
		*/
	}
	else
	{
		if (gl_object_isa(par, "switch", "powerflow") == false)
		{
			GL_THROW("sync_check:%d %s the parent object must be a powerflow switch object!",
					 obj->id, (obj->name ? obj->name : "Unnamed"));
			/*  TROUBLESHOOT
			The parent object must be a powerflow switch object. Please try again.
			If the error persists, please submit your GLM and a bug report to the ticketing system.
			*/
		}
	}

	// Check the status of the 'switch_object' object (when it is armed, the parent switch should be in 'OPEN' status)
	swt_prop_status = new gld_property(par, "status");

	// Double check the validity of the nominal frequency property
	if ((swt_prop_status->is_valid() != true) || (swt_prop_status->is_enumeration() != true))
	{
		GL_THROW("sync_check:%d %s failed to map the swtich status property",
				 obj->id, (obj->name ? obj->name : "Unnamed"));
		/*  TROUBLESHOOT
		While attempting to map the swtich status property, an error occurred.  Please try again.
		If the error persists, please submit your GLM and a bug report to the ticketing system.
		*/
	}

	if (sc_enabled_flag)
	{
		enumeration swt_init_status = swt_prop_status->get_enumeration();
		if (swt_init_status != LS_OPEN)
		{
			sc_enabled_flag = false; // disarmed itself
			gl_warning("sync_check:%d %s the parent switch_object object is starting CLOSED, so sync_check object is disarmed!",
					   obj->id, (obj->name ? obj->name : "Unnamed"));
			/*  TROUBLESHOOT
			The parent switch_object object is CLOSED. Thus, the sync_check object is disarmed since it won't need to close the switch.
			If the warning persists and the switch is open, please submit your code and	a bug report via the issue tracker.
			*/
		}
	}

	// Check the params
	if (frequency_tolerance_hz <= 0)
	{


		/* Get the nominal frequency property */
		temp_property_pointer = new gld_property("powerflow::nominal_frequency");

		// Double check the validity of the nominal frequency property
		if ((temp_property_pointer->is_valid() != true) || (temp_property_pointer->is_double() != true))
		{
			GL_THROW("sync_check:%d %s failed to map the nominal_frequency property", obj->id, (obj->name ? obj->name : "Unnamed"));
			//Defined above
		}

		// Get the value of nominal frequency from this property
		temp_freq_val = temp_property_pointer->get_double();

		// Clean the temporary property pointer
		delete temp_property_pointer;

		//Default it to 1%
		frequency_tolerance_hz = 0.01 * temp_freq_val;

		gl_warning("sync_check:%d %s - frequency_tolerance was not set as a positive value, it is reset to %f [Hz].",
				   obj->id, (obj->name ? obj->name : "Unnamed"), frequency_tolerance_hz);
		/*  TROUBLESHOOT
		The frequency_tolerance was not set as a positive value!
		If the warning persists and the object does, please submit your code and
		a bug report via the issue tracker.
		*/
	}

	// The voltage tolerance settings of both modes are checked, regardless of the mode at init as the mode may be modified later on
	if (voltage_tolerance_pu <= 0)
	{
		voltage_tolerance_pu = 1e-2; // i.e., 1%
		gl_warning("sync_check:%d %s - voltage_tolerance_pu was not set as a positive value, it is reset to %f [pu].",
				   obj->id, (obj->name ? obj->name : "Unnamed"), voltage_tolerance_pu);
		/*  TROUBLESHOOT
		The voltage_tolerance_pu was not set as a positive value!
		If the warning persists and the object does, please submit your code and
		a bug report via the issue tracker.
		*/
	}

	if (voltage_magnitude_tolerance_pu <= 0)
	{
		voltage_magnitude_tolerance_pu = 1e-2; // i.e, 1%
		gl_warning("sync_check:%d %s - %s was not set as a positive value, it is reset to %f [pu].",
				   obj->id, (obj->name ? obj->name : "Unnamed"),
				   STR(voltage_magnitude_tolerance_pu), voltage_magnitude_tolerance_pu);
		/*  TROUBLESHOOT
		The voltage_magnitude_tolerance was not set as a positive value!
		If the warning persists and the object does, please submit your code and
		a bug report via the issue tracker.
		*/
	}

	if (voltage_angle_tolerance_deg <= 0)
	{
		voltage_angle_tolerance_deg = 5; // Unit: Degree
		gl_warning("sync_check:%d %s - voltage_angle_tolerance was not set as a positive value, it is reset to %f [Deg].",
				   obj->id, (obj->name ? obj->name : "Unnamed"), voltage_angle_tolerance_deg);
		/*  TROUBLESHOOT
		The voltage_angle_tolerance was not set as a positive value!
		If the warning persists and the object does, please submit your code and
		a bug report via the issue tracker.
		*/
	}

	if (metrics_period_sec <= 0)
	{
		metrics_period_sec = 1.2; // i.e., 1.2 secs
		gl_warning("sync_check:%d %s - metrics_period_sec was not set as a positive value, it is reset to %f [secs].",
				   obj->id, (obj->name ? obj->name : "Unnamed"), metrics_period_sec);
		/*  TROUBLESHOOT
		The metrics_period_sec was not set as a positive value!
		If the warning persists and the object does, please submit your code and
		a bug report via the issue tracker.
		*/
	}
}

void sync_check::reg_deltamode_check()
{
	OBJECT *obj = OBJECTHDR(this);

	// Set the deltamode flag, if desired
	if ((obj->flags & OF_DELTAMODE) == OF_DELTAMODE)
	{
		deltamode_inclusive = true; //Set the flag and off we go
	}

	// Check the module deltamode flag & the object deltamode flag for consistency
	if (enable_subsecond_models)
	{
		if (deltamode_inclusive != true)
		{
			gl_warning("sync_check:%d %s - Deltamode is enabled for the powerflow module, but not this sync_check object!",
					   obj->id, (obj->name ? obj->name : "Unnamed"));
			/*  TROUBLESHOOT
			Deltamode is enabled for the powerflow module, but not this sync_check object!
			If the warning persists and the object does, please submit your code and
			a bug report via the issue tracker.
			*/
		}
		else // Both the powerflow module and object are enabled for the deltamode
		{
			pwr_object_count++;
			reg_dm_flag = true;
		}
	}
	else
	{
		if (deltamode_inclusive == true)
		{
			gl_warning("sync_check:%d %s - Deltamode is enabled for the sync_check object, but not this powerflow module!",
					   obj->id, (obj->name ? obj->name : "Unnamed"));
			/*  TROUBLESHOOT
			Deltamode is enabled for the sync_check object, but not this powerflow module!
			If the warning persists and the object does, please submit your code and
			a bug report via the issue tracker.
			*/
		}
	}
}

void sync_check::reg_deltamode()
{
	// Check if this object needs to be registered for running in deltamode
	if (reg_dm_flag)
	{
		// Turn off this one-time flag
		reg_dm_flag = false;

		//Check limits first
		if (pwr_object_current >= pwr_object_count)
		{
			GL_THROW("Too many objects tried to populate deltamode objects array in the powerflow module!");
			/*  TROUBLESHOOT
			While attempting to populate a reference array of deltamode-enabled objects for the powerflow
			module, an attempt was made to write beyond the allocated array space.  Please try again.  If the
			error persists, please submit a bug report and your code via the issue tracker.
			*/
		}

		// Get the self-pointer
		OBJECT *obj = OBJECTHDR(this);

		// Add this object into the list of deltamode objects
		delta_objects[pwr_object_current] = obj;

		// Map up the function
		delta_functions[pwr_object_current] = (FUNCTIONADDR)(gl_get_function(obj, "interupdate_pwr_object"));

		// Dobule check the mapped function
		if (delta_functions[pwr_object_current] == NULL)
		{
			gl_warning("Failure to map deltamode function for this device: %s", obj->name);
			/*  TROUBLESHOOT
			Attempts to map up the interupdate function of a specific device failed.  Please try again and ensure
			the object supports deltamode.  This warning may simply be an indication that the object of interest
			does not support deltamode.  If the warning persists and the object does, please submit your code and
			a bug report via the issue tracker.
			*/
		}

		// Set the post delta function to NULL, thus it does not need to be checked
		post_delta_functions[pwr_object_current] = NULL;

		//Increment
		pwr_object_current++;
	}
}

void sync_check::init_norm_values(OBJECT *par)
{
	// Get the self-pointer
	OBJECT *obj = OBJECTHDR(this);

	/* Get the from node */
	temp_property_pointer = new gld_property(par, "from");
	// Double check the validity
	if ((temp_property_pointer->is_valid() != true) || (temp_property_pointer->is_objectref() != true))
	{
		GL_THROW("sync_check:%d %s Failed to map the switch property 'from'!",
				 obj->id, (obj->name ? obj->name : "Unnamed"));
		/*  TROUBLESHOOT
        While attempting to map the a property from the switch object, an error occurred.  Please try again.
        If the error persists, please submit your GLM and a bug report to the ticketing system.
        */
	}

	swt_fm_node = temp_property_pointer->get_objectref();
	delete temp_property_pointer;

	/* Get the to node */
	temp_property_pointer = new gld_property(par, "to");
	// Double check the validity
	if ((temp_property_pointer->is_valid() != true) || (temp_property_pointer->is_objectref() != true))
	{
		GL_THROW("sync_check:%d %s Failed to map the switch property 'to'!",
				 obj->id, (obj->name ? obj->name : "Unnamed"));
		/*  TROUBLESHOOT
        While attempting to map the a property from the switch object, an error occurred.  Please try again.
        If the error persists, please submit your GLM and a bug report to the ticketing system.
        */
	}

	swt_to_node = temp_property_pointer->get_objectref();
	delete temp_property_pointer;

	/* Get the norminal voltage & check the consistency */
	// 'From' node voltage
	temp_property_pointer = new gld_property(swt_fm_node, "nominal_voltage");
	// Double check the validity of the nominal frequency property
	if ((temp_property_pointer->is_valid() != true) || (temp_property_pointer->is_double() != true))
	{
		GL_THROW("sync_check:%d %s failed to map the nominal_voltage property",
				 obj->id, (obj->name ? obj->name : "Unnamed"));
		/*  TROUBLESHOOT
		While attempting to map the nominal_voltage property, an error occurred.  Please try again.
		If the error persists, please submit your GLM and a bug report to the ticketing system.
		*/
	}
	double volt_norm_fm = temp_property_pointer->get_double();
	delete temp_property_pointer;

	// 'To' node voltage
	temp_property_pointer = new gld_property(swt_to_node, "nominal_voltage");
	// Double check the validity of the nominal frequency property
	if ((temp_property_pointer->is_valid() != true) || (temp_property_pointer->is_double() != true))
	{
		GL_THROW("sync_check:%d %s failed to map the nominal_voltage property",
				 obj->id, (obj->name ? obj->name : "Unnamed"));
		/*  TROUBLESHOOT
		While attempting to map the nominal_voltage property, an error occurred.  Please try again.
		If the error persists, please submit your GLM and a bug report to the ticketing system.
		*/
	}
	double volt_norm_to = temp_property_pointer->get_double();
	delete temp_property_pointer;

	// Check consistency
	volt_norm = (volt_norm_fm + volt_norm_to) / 2;
	if (abs(volt_norm_fm - volt_norm_to) > (voltage_tolerance_pu * volt_norm))
	{
		GL_THROW("sync_check:%d %s nominal_voltage on the from and to nodes of the switch should be close enough!",
				 obj->id, (obj->name ? obj->name : "Unnamed"));
		/*  TROUBLESHOOT
		While checking the nominal voltages of the 'from' and 'to' nodes of the switch_object, an error occurred.  Please try again.
		If the error persists, please submit your GLM and a bug report to the ticketing system.
		*/
	}

	//Populate default tolerances for deltamode triggers
	if (delta_trigger_mult <= 1.0)
	{
		delta_trigger_mult = 2.0;
		gl_warning("sync_check:%d %s - delta_trigger_mult was below 1.0 - defaulted to 2.0",obj->id, (obj->name ? obj->name : "Unnamed"));
		/*  TROUBLESHOOT
		The delta_trigger_mult value was below, which isn't allowed.  Set this to a positive number larger than 1.0 to be a proper trigger
		point for deltamode.
		*/
	}
	frequency_tolerance_hz_deltamode_trig = delta_trigger_mult * frequency_tolerance_hz;
	voltage_tolerance_pu_deltamode_trig = delta_trigger_mult * voltage_tolerance_pu;
	voltage_magnitude_tolerance_pu_deltamode_trig = delta_trigger_mult * voltage_magnitude_tolerance_pu;
	voltage_angle_tolerance_deg_deltamode_trig = delta_trigger_mult * voltage_angle_tolerance_deg;

	//Set initial value
	next_trigger_update_time = gl_globalclock;
}

void sync_check::update_measurements()
{
	//@Todo: 1) validity check of properties; 2) frequency of each phase?

	/* Get the 'from' node freq */
	swt_fm_node_freq = prop_fm_node_freq->get_double();

	/* Get the 'to' node freq */
	swt_to_node_freq = prop_to_node_freq->get_double();

	/* Get the 'from' & 'to' node volt phasors */
	if (swt_ph_A_flag)
	{
		swt_fm_volt_A = prop_fm_node_volt_A->get_complex();
		swt_to_volt_A = prop_to_node_volt_A->get_complex();
	}

	if (swt_ph_B_flag)
	{
		swt_fm_volt_B = prop_fm_node_volt_B->get_complex();
		swt_to_volt_B = prop_to_node_volt_B->get_complex();
	}

	if (swt_ph_C_flag)
	{
		swt_fm_volt_C = prop_fm_node_volt_C->get_complex();
		swt_to_volt_C = prop_to_node_volt_C->get_complex();
	}
}

void sync_check::check_metrics(bool deltamode_check)
{
	double freq_diff_hz = abs(swt_fm_node_freq - swt_to_node_freq);

	if (volt_compare_mode == MAG_DIFF)
	{
		double volt_A_diff = (swt_fm_volt_A - swt_to_volt_A).Mag();
		double volt_A_diff_pu = volt_A_diff / volt_norm;

		double volt_B_diff = (swt_fm_volt_B - swt_to_volt_B).Mag();
		double volt_B_diff_pu = volt_B_diff / volt_norm;

		double volt_C_diff = (swt_fm_volt_C - swt_to_volt_C).Mag();
		double volt_C_diff_pu = volt_C_diff / volt_norm;

		if (deltamode_check == true)
		{
			if ((freq_diff_hz <= frequency_tolerance_hz_deltamode_trig) && (volt_A_diff_pu <= voltage_tolerance_pu_deltamode_trig) &&
				(volt_B_diff_pu <= voltage_tolerance_pu_deltamode_trig) && (volt_C_diff_pu <= voltage_tolerance_pu_deltamode_trig))
			{
				deltamode_trigger_keep_flag = true;
			}
			else
			{
				deltamode_trigger_keep_flag = false;
			}
		}//End "stay/trigger deltamode" check
		else	//Standard "closure" check
		{
			if ((freq_diff_hz <= frequency_tolerance_hz) && (volt_A_diff_pu <= voltage_tolerance_pu) &&
				(volt_B_diff_pu <= voltage_tolerance_pu) && (volt_C_diff_pu <= voltage_tolerance_pu))
			{
				metrics_flag = true;
			}
			else
			{
				metrics_flag = false;
			}
		}//End standard closure check
	}
	else // SEP_DIFF Mode
	{
		double volt_A_ang_deg_diff, volt_B_ang_deg_diff, volt_C_ang_deg_diff;

		// Phase A
		double volt_A_mag_diff = abs(swt_fm_volt_A.Mag() - swt_to_volt_A.Mag());
		double volt_A_mag_diff_pu = volt_A_mag_diff / volt_norm;

		double volt_A_ang_rad_diff = abs(swt_fm_volt_A.Arg() - swt_to_volt_A.Arg());
		double volt_A_ang_deg_diff_temp = RAD_TO_DEG(volt_A_ang_rad_diff);
		
		//Adjust calculation to reflect the odd wrapping of angles, as well as magnitude
		if (volt_A_ang_deg_diff_temp > 180.0)
			volt_A_ang_deg_diff = 360.0 - volt_A_ang_deg_diff_temp;
		else
			volt_A_ang_deg_diff = volt_A_ang_deg_diff_temp;

		// Phase B
		double volt_B_mag_diff = abs(swt_fm_volt_B.Mag() - swt_to_volt_B.Mag());
		double volt_B_mag_diff_pu = volt_B_mag_diff / volt_norm;

		double volt_B_ang_rad_diff = abs(swt_fm_volt_B.Arg() - swt_to_volt_B.Arg());
		double volt_B_ang_deg_diff_temp = RAD_TO_DEG(volt_B_ang_rad_diff);

		//Adjust calculation to reflect the odd wrapping of angles, as well as magnitude
		if (volt_B_ang_deg_diff_temp > 180.0)
			volt_B_ang_deg_diff = 360.0 - volt_B_ang_deg_diff_temp;
		else
			volt_B_ang_deg_diff = volt_B_ang_deg_diff_temp;

		// Phase C
		double volt_C_mag_diff = abs(swt_fm_volt_C.Mag() - swt_to_volt_C.Mag());
		double volt_C_mag_diff_pu = volt_C_mag_diff / volt_norm;

		double volt_C_ang_rad_diff = abs(swt_fm_volt_C.Arg() - swt_to_volt_C.Arg());
		double volt_C_ang_deg_diff_temp = RAD_TO_DEG(volt_C_ang_rad_diff);

		//Adjust calculation to reflect the odd wrapping of angles, as well as magnitude
		if (volt_C_ang_deg_diff_temp > 180.0)
			volt_C_ang_deg_diff = 360.0 - volt_C_ang_deg_diff_temp;
		else
			volt_C_ang_deg_diff = volt_C_ang_deg_diff_temp;

		// Check
		if (deltamode_check == true)
		{
			if ((freq_diff_hz <= frequency_tolerance_hz_deltamode_trig) &&
				(volt_A_mag_diff_pu <= voltage_magnitude_tolerance_pu_deltamode_trig) &&
				(volt_B_mag_diff_pu <= voltage_magnitude_tolerance_pu_deltamode_trig) && (volt_C_mag_diff_pu <= voltage_magnitude_tolerance_pu_deltamode_trig) &&
				(volt_A_ang_deg_diff <= voltage_angle_tolerance_deg_deltamode_trig) &&
				(volt_B_ang_deg_diff <= voltage_angle_tolerance_deg_deltamode_trig) && (volt_C_ang_deg_diff <= voltage_angle_tolerance_deg_deltamode_trig))
			{
				deltamode_trigger_keep_flag = true;
			}
			else
			{
				deltamode_trigger_keep_flag = false;
			}
		}//End deltamode trigger/maintain check
		else	//Standard "closure check"
		{
			if ((freq_diff_hz <= frequency_tolerance_hz) &&
				(volt_A_mag_diff_pu <= voltage_magnitude_tolerance_pu) &&
				(volt_B_mag_diff_pu <= voltage_magnitude_tolerance_pu) && (volt_C_mag_diff_pu <= voltage_magnitude_tolerance_pu) &&
				(volt_A_ang_deg_diff <= voltage_angle_tolerance_deg) &&
				(volt_B_ang_deg_diff <= voltage_angle_tolerance_deg) && (volt_C_ang_deg_diff <= voltage_angle_tolerance_deg))
			{
				metrics_flag = true;
			}
			else
			{
				metrics_flag = false;
			}
		}//End standard closure check
	}
}

void sync_check::check_excitation(unsigned long dt)
{
	double dt_dm_sec = (double)dt / (double)DT_SECOND;

	if (metrics_flag)
	{
		t_sat += dt_dm_sec;
	}
	else
	{
		t_sat = 0;
	}

	if (t_sat >= metrics_period_sec)
	{
		gld_wlock *test_rlock;
		enumeration swt_cmd = LS_CLOSED;
		swt_prop_status->setp<enumeration>(swt_cmd, *test_rlock); // Close the switch for parallelling
		reset_after_excitation();
	}
}

void sync_check::reset_after_excitation()
{
	// After closing the swtich, disable itself
	sc_enabled_flag = false; //Unarmed

	// Reset other buffers & variables
	metrics_flag = false;
	t_sat = 0;
}

//Deltamode call
//Module-level call
SIMULATIONMODE sync_check::inter_deltaupdate_sync_check(unsigned int64 delta_time, unsigned long dt, unsigned int iteration_count_val, bool interupdate_pos)
{
	if (sc_enabled_flag)
	{
		if ((iteration_count_val == 0) && (!interupdate_pos)) //@TODO: Need further testings
		{
			update_measurements();

			//Do the deltamode trigger check
			check_metrics(true);

			//See how to proceed
			if (deltamode_trigger_keep_flag == true)	//In bounds, track!
			{
				deltamode_check_return_val = SM_DELTA;
			}
			else	//Out of bounds, just let other objects drive us
			{
				deltamode_check_return_val = SM_EVENT;
			}

			//Actual sync_check tests
			check_metrics(false);
			check_excitation(dt);
		}
		//Default else - other passes, just return whatever value we had set
	}
	else	//Not enabled, so something else gets to drive deltamode
	{
		//Paranoia set the flag, just in case
		deltamode_trigger_keep_flag = false;

		//Set return status
		deltamode_check_return_val = SM_EVENT;

		//Reset accumulator timer, in case disabled before a closing happened
		t_sat = 0.0;
	}
	
	return deltamode_check_return_val;
}

//////////////////////////////////////////////////////////////////////////
// IMPLEMENTATION OF CORE LINKAGE: sync_check
//////////////////////////////////////////////////////////////////////////

/**
* REQUIRED: allocate and initialize an object.
*
* @param obj a pointer to a pointer of the last object in the list
* @param parent a pointer to the parent of this object
* @return 1 for a successfully created object, 0 for error
*/
EXPORT int create_sync_check(OBJECT **obj, OBJECT *parent)
{
	try
	{
		*obj = gl_create_object(sync_check::oclass);
		if (*obj != NULL)
		{
			sync_check *my = OBJECTDATA(*obj, sync_check);
			gl_set_parent(*obj, parent);
			return my->create();
		}
		else
			return 0;
	}
	CREATE_CATCHALL(sync_check);
}

EXPORT int init_sync_check(OBJECT *obj)
{
	try
	{
		sync_check *my = OBJECTDATA(obj, sync_check);
		return my->init(obj->parent);
	}
	INIT_CATCHALL(sync_check);
}

/**
* Sync is called when the clock needs to advance on the bottom-up pass (PC_BOTTOMUP)
*
* @param obj the object we are sync'ing
* @param t0 this objects current timestamp
* @param pass the current pass for this sync call
* @return t1, where t1>t0 on success, t1=t0 for retry, t1<t0 on failure
*/
EXPORT TIMESTAMP sync_sync_check(OBJECT *obj, TIMESTAMP t0, PASSCONFIG pass)
{
	try
	{
		sync_check *pObj = OBJECTDATA(obj, sync_check);
		TIMESTAMP t1 = TS_NEVER;
		switch (pass)
		{
		case PC_PRETOPDOWN:
			return pObj->presync(t0);
		case PC_BOTTOMUP:
			return pObj->sync(t0);
		case PC_POSTTOPDOWN:
			t1 = pObj->postsync(t0);
			obj->clock = t0;
			return t1;
		default:
			throw "invalid pass request";
		}
	}
	SYNC_CATCHALL(sync_check);
}

EXPORT int isa_sync_check(OBJECT *obj, char *classname)
{
	return OBJECTDATA(obj, sync_check)->isa(classname);
}

//Deltamode export
EXPORT SIMULATIONMODE interupdate_sync_check(OBJECT *obj, unsigned int64 delta_time, unsigned long dt, unsigned int iteration_count_val, bool interupdate_pos)
{
	sync_check *my = OBJECTDATA(obj, sync_check);
	SIMULATIONMODE status = SM_ERROR;
	try
	{
		status = my->inter_deltaupdate_sync_check(delta_time, dt, iteration_count_val, interupdate_pos);
		return status;
	}
	catch (char *msg)
	{
		gl_error("interupdate_sync_check(obj=%d;%s): %s", obj->id, obj->name ? obj->name : "unnamed", msg);
		return status;
	}
}


/*
 * Copyright (C) 2000 - 2012, Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#include <linux/export.h>
#include <acpi/acpi.h>
#include "accommon.h"
#include "acevents.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_EVENTS
ACPI_MODULE_NAME("evxfgpe")

#if (!ACPI_REDUCED_HARDWARE)	

acpi_status acpi_update_all_gpes(void)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_update_all_gpes);

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	if (acpi_gbl_all_gpes_initialized) {
		goto unlock_and_exit;
	}

	status = acpi_ev_walk_gpe_list(acpi_ev_initialize_gpe_block, NULL);
	if (ACPI_SUCCESS(status)) {
		acpi_gbl_all_gpes_initialized = TRUE;
	}

unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);

	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_update_all_gpes)


acpi_status acpi_enable_gpe(acpi_handle gpe_device, u32 gpe_number)
{
	acpi_status status = AE_BAD_PARAMETER;
	struct acpi_gpe_event_info *gpe_event_info;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(acpi_enable_gpe);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (gpe_event_info) {
		status = acpi_ev_add_gpe_reference(gpe_event_info);
	}

	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}
ACPI_EXPORT_SYMBOL(acpi_enable_gpe)


acpi_status acpi_disable_gpe(acpi_handle gpe_device, u32 gpe_number)
{
	acpi_status status = AE_BAD_PARAMETER;
	struct acpi_gpe_event_info *gpe_event_info;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(acpi_disable_gpe);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (gpe_event_info) {
		status = acpi_ev_remove_gpe_reference(gpe_event_info) ;
	}

	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}
ACPI_EXPORT_SYMBOL(acpi_disable_gpe)


acpi_status
acpi_setup_gpe_for_wake(acpi_handle wake_device,
			acpi_handle gpe_device, u32 gpe_number)
{
	acpi_status status = AE_BAD_PARAMETER;
	struct acpi_gpe_event_info *gpe_event_info;
	struct acpi_namespace_node *device_node;
	struct acpi_gpe_notify_object *notify_object;
	acpi_cpu_flags flags;
	u8 gpe_dispatch_mask;

	ACPI_FUNCTION_TRACE(acpi_setup_gpe_for_wake);

	

	if (!wake_device) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info) {
		goto unlock_and_exit;
	}

	if (wake_device == ACPI_ROOT_OBJECT) {
		goto out;
	}

	gpe_dispatch_mask = gpe_event_info->flags & ACPI_GPE_DISPATCH_MASK;
	if (gpe_dispatch_mask != ACPI_GPE_DISPATCH_NONE
	    && gpe_dispatch_mask != ACPI_GPE_DISPATCH_NOTIFY) {
		goto out;
	}

	

	device_node = ACPI_CAST_PTR(struct acpi_namespace_node, wake_device);
	if (device_node->type != ACPI_TYPE_DEVICE) {
		goto unlock_and_exit;
	}

	if (gpe_dispatch_mask == ACPI_GPE_DISPATCH_NONE) {
		gpe_event_info->flags = (ACPI_GPE_DISPATCH_NOTIFY |
					 ACPI_GPE_LEVEL_TRIGGERED);
		gpe_event_info->dispatch.device.node = device_node;
		gpe_event_info->dispatch.device.next = NULL;
	} else {
		

		notify_object = ACPI_ALLOCATE_ZEROED(sizeof(*notify_object));
		if (!notify_object) {
			status = AE_NO_MEMORY;
			goto unlock_and_exit;
		}

		notify_object->node = device_node;
		notify_object->next = gpe_event_info->dispatch.device.next;
		gpe_event_info->dispatch.device.next = notify_object;
	}

 out:
	gpe_event_info->flags |= ACPI_GPE_CAN_WAKE;
	status = AE_OK;

 unlock_and_exit:
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}
ACPI_EXPORT_SYMBOL(acpi_setup_gpe_for_wake)


acpi_status acpi_set_gpe_wake_mask(acpi_handle gpe_device, u32 gpe_number, u8 action)
{
	acpi_status status = AE_OK;
	struct acpi_gpe_event_info *gpe_event_info;
	struct acpi_gpe_register_info *gpe_register_info;
	acpi_cpu_flags flags;
	u32 register_bit;

	ACPI_FUNCTION_TRACE(acpi_set_gpe_wake_mask);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	if (!(gpe_event_info->flags & ACPI_GPE_CAN_WAKE)) {
		status = AE_TYPE;
		goto unlock_and_exit;
	}

	gpe_register_info = gpe_event_info->register_info;
	if (!gpe_register_info) {
		status = AE_NOT_EXIST;
		goto unlock_and_exit;
	}

	register_bit =
	    acpi_hw_get_gpe_register_bit(gpe_event_info, gpe_register_info);

	

	switch (action) {
	case ACPI_GPE_ENABLE:
		ACPI_SET_BIT(gpe_register_info->enable_for_wake,
			     (u8)register_bit);
		break;

	case ACPI_GPE_DISABLE:
		ACPI_CLEAR_BIT(gpe_register_info->enable_for_wake,
			       (u8)register_bit);
		break;

	default:
		ACPI_ERROR((AE_INFO, "%u, Invalid action", action));
		status = AE_BAD_PARAMETER;
		break;
	}

unlock_and_exit:
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_set_gpe_wake_mask)

acpi_status acpi_clear_gpe(acpi_handle gpe_device, u32 gpe_number)
{
	acpi_status status = AE_OK;
	struct acpi_gpe_event_info *gpe_event_info;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(acpi_clear_gpe);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	status = acpi_hw_clear_gpe(gpe_event_info);

      unlock_and_exit:
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_clear_gpe)

acpi_status
acpi_get_gpe_status(acpi_handle gpe_device,
		    u32 gpe_number, acpi_event_status *event_status)
{
	acpi_status status = AE_OK;
	struct acpi_gpe_event_info *gpe_event_info;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(acpi_get_gpe_status);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	

	status = acpi_hw_get_gpe_status(gpe_event_info, event_status);

	if (gpe_event_info->flags & ACPI_GPE_DISPATCH_MASK)
		*event_status |= ACPI_EVENT_FLAG_HANDLE;

      unlock_and_exit:
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_get_gpe_status)


acpi_status acpi_disable_all_gpes(void)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_disable_all_gpes);

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_hw_disable_all_gpes();
	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);

	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_disable_all_gpes)


acpi_status acpi_enable_all_runtime_gpes(void)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_enable_all_runtime_gpes);

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_hw_enable_all_runtime_gpes();
	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);

	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_enable_all_runtime_gpes)

acpi_status
acpi_install_gpe_block(acpi_handle gpe_device,
		       struct acpi_generic_address *gpe_block_address,
		       u32 register_count, u32 interrupt_number)
{
	acpi_status status;
	union acpi_operand_object *obj_desc;
	struct acpi_namespace_node *node;
	struct acpi_gpe_block_info *gpe_block;

	ACPI_FUNCTION_TRACE(acpi_install_gpe_block);

	if ((!gpe_device) || (!gpe_block_address) || (!register_count)) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	node = acpi_ns_validate_handle(gpe_device);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	status =
	    acpi_ev_create_gpe_block(node, gpe_block_address, register_count, 0,
				     interrupt_number, &gpe_block);
	if (ACPI_FAILURE(status)) {
		goto unlock_and_exit;
	}

	

	obj_desc = acpi_ns_get_attached_object(node);
	if (!obj_desc) {

		obj_desc = acpi_ut_create_internal_object(ACPI_TYPE_DEVICE);
		if (!obj_desc) {
			status = AE_NO_MEMORY;
			goto unlock_and_exit;
		}

		status =
		    acpi_ns_attach_object(node, obj_desc, ACPI_TYPE_DEVICE);

		

		acpi_ut_remove_reference(obj_desc);

		if (ACPI_FAILURE(status)) {
			goto unlock_and_exit;
		}
	}

	

	obj_desc->device.gpe_block = gpe_block;

      unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_install_gpe_block)

acpi_status acpi_remove_gpe_block(acpi_handle gpe_device)
{
	union acpi_operand_object *obj_desc;
	acpi_status status;
	struct acpi_namespace_node *node;

	ACPI_FUNCTION_TRACE(acpi_remove_gpe_block);

	if (!gpe_device) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	node = acpi_ns_validate_handle(gpe_device);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	

	obj_desc = acpi_ns_get_attached_object(node);
	if (!obj_desc || !obj_desc->device.gpe_block) {
		return_ACPI_STATUS(AE_NULL_OBJECT);
	}

	

	status = acpi_ev_delete_gpe_block(obj_desc->device.gpe_block);
	if (ACPI_SUCCESS(status)) {
		obj_desc->device.gpe_block = NULL;
	}

      unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_remove_gpe_block)

acpi_status
acpi_get_gpe_device(u32 index, acpi_handle *gpe_device)
{
	struct acpi_gpe_device_info info;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_get_gpe_device);

	if (!gpe_device) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (index >= acpi_current_gpe_count) {
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	

	info.index = index;
	info.status = AE_NOT_EXIST;
	info.gpe_device = NULL;
	info.next_block_base_index = 0;

	status = acpi_ev_walk_gpe_list(acpi_ev_get_gpe_device, &info);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	*gpe_device = ACPI_CAST_PTR(acpi_handle, info.gpe_device);
	return_ACPI_STATUS(info.status);
}

ACPI_EXPORT_SYMBOL(acpi_get_gpe_device)
#endif				


// This file is part of the mstp-lib library, available at https://github.com/adigostin/mstp-lib
// Copyright (c) 2011-2020 Adi Gostin, distributed under Apache License v2.0.

#include "pch.h"
#include "bridge_tree.h"
#include "bridge.h"

using namespace edge;

bridge_tree::bridge_tree (bridge* parent, size_t tree_index)
	: _parent(parent), _tree_index(tree_index)
{
	// No need to call remove_handler since a bridge and its bridge_trees are deleted at the same time.
	_parent->property_changing().add_handler(&on_bridge_property_changing, this);
	_parent->property_changed().add_handler(&on_bridge_property_changed, this);

	::GetSystemTime(&_last_topology_change);
	_topology_change_count = 0;
}

static const value_property* const properties_changed_on_stp_enable_disable[] = {
	&bridge_tree::root_id_property,
	&bridge_tree::external_root_path_cost_property,
	&bridge_tree::regional_root_id_property,
	&bridge_tree::internal_root_path_cost_property,
	&bridge_tree::designated_bridge_id_property,
	&bridge_tree::designated_port_id_property,
	&bridge_tree::receiving_port_id_property,
};

void bridge_tree::on_bridge_property_changing (void* arg, object* obj, const property_change_args& args)
{
	auto bt = static_cast<bridge_tree*>(arg);
	for (auto it = std::begin(properties_changed_on_stp_enable_disable); it != std::end(properties_changed_on_stp_enable_disable); it++)
		bt->on_property_changing(*it);
}

void bridge_tree::on_bridge_property_changed (void* arg, object* obj, const property_change_args& args)
{
	auto bt = static_cast<bridge_tree*>(arg);
	for (auto it = std::rbegin(properties_changed_on_stp_enable_disable); it != std::rend(properties_changed_on_stp_enable_disable); it++)
		bt->on_property_changed(*it);
}

void bridge_tree::on_topology_change (unsigned int timestamp)
{
	this->on_property_changing(&topology_change_count_property);
	::GetSystemTime(&_last_topology_change);
	_topology_change_count++;
	this->on_property_changed(&topology_change_count_property);
}

uint32_t bridge_tree::bridge_priority() const
{
	return STP_GetBridgePriority (_parent->stp_bridge(), (unsigned int)_tree_index);
}

void bridge_tree::set_bridge_priority (uint32_t priority)
{
	if (bridge_priority() != priority)
	{
		this->on_property_changing(&bridge_priority_property);
		STP_SetBridgePriority (_parent->stp_bridge(), (unsigned int)_tree_index, (unsigned short) priority, GetMessageTime());
		this->on_property_changed(&bridge_priority_property);
	}
}

std::array<unsigned char, 36> bridge_tree::root_priorty_vector() const
{
	std::array<unsigned char, 36> prioVector;
	STP_GetRootPriorityVector(_parent->stp_bridge(), (unsigned int)_tree_index, prioVector.data());
	return prioVector;
}

std::string bridge_tree::root_bridge_id() const
{
	if (!STP_IsBridgeStarted (_parent->stp_bridge()))
		throw std::logic_error(stp_disabled_text);

	auto rpv = root_priorty_vector();
	std::stringstream ss;
	ss << std::uppercase << std::setfill('0') << std::hex
		<< std::setw(2) << (int)rpv[0] << std::setw(2) << (int)rpv[1] << "."
		<< std::setw(2) << (int)rpv[2] << std::setw(2) << (int)rpv[3] << std::setw(2) << (int)rpv[4]
		<< std::setw(2) << (int)rpv[5] << std::setw(2) << (int)rpv[6] << std::setw(2) << (int)rpv[7];
	return ss.str();
}

uint32_t bridge_tree::external_root_path_cost() const
{
	if (!STP_IsBridgeStarted(_parent->stp_bridge()))
		throw std::logic_error(stp_disabled_text);

	auto rpv = root_priorty_vector();
	auto cost = ((uint32_t) rpv[8] << 24) | ((uint32_t) rpv[9] << 16) | ((uint32_t) rpv[10] << 8) | rpv[11];
	return cost;
}

std::string bridge_tree::regional_root_id() const
{
	if (!STP_IsBridgeStarted (_parent->stp_bridge()))
		throw std::logic_error(stp_disabled_text);

	auto rpv = root_priorty_vector();
	std::stringstream ss;
	ss << std::uppercase << std::setfill('0') << std::hex
		<< std::setw(2) << (int)rpv[12] << std::setw(2) << (int)rpv[13] << "."
		<< std::setw(2) << (int)rpv[14] << std::setw(2) << (int)rpv[15] << std::setw(2) << (int)rpv[16]
		<< std::setw(2) << (int)rpv[17] << std::setw(2) << (int)rpv[18] << std::setw(2) << (int)rpv[19];
	return ss.str();
}

uint32_t bridge_tree::internal_root_path_cost() const
{
	if (!STP_IsBridgeStarted(_parent->stp_bridge()))
		throw std::logic_error(stp_disabled_text);

	auto rpv = root_priorty_vector();
	auto cost = ((uint32_t) rpv[20] << 24) | ((uint32_t) rpv[21] << 16) | ((uint32_t) rpv[22] << 8) | rpv[23];
	return cost;
}

std::string bridge_tree::designated_bridge_id() const
{
	if (!STP_IsBridgeStarted(_parent->stp_bridge()))
		throw std::logic_error(stp_disabled_text);

	auto rpv = root_priorty_vector();
	std::stringstream ss;
	ss << std::uppercase << std::setfill('0') << std::hex
		<< std::setw(2) << (int)rpv[24] << std::setw(2) << (int)rpv[25] << "."
		<< std::setw(2) << (int)rpv[26] << std::setw(2) << (int)rpv[27] << std::setw(2) << (int)rpv[28]
		<< std::setw(2) << (int)rpv[29] << std::setw(2) << (int)rpv[30] << std::setw(2) << (int)rpv[31];
	return ss.str();
}

std::string bridge_tree::designated_port_id() const
{
	if (!STP_IsBridgeStarted(_parent->stp_bridge()))
		throw std::logic_error(stp_disabled_text);

	auto rpv = root_priorty_vector();
	std::stringstream ss;
	ss << std::uppercase << std::setfill('0') << std::hex << std::setw(2) << (int)rpv[32] << std::setw(2) << (int)rpv[33];
	return ss.str();
}

std::string bridge_tree::receiving_port_id() const
{
	if (!STP_IsBridgeStarted(_parent->stp_bridge()))
		throw std::logic_error(stp_disabled_text);

	auto rpv = root_priorty_vector();
	std::stringstream ss;
	ss << std::uppercase << std::setfill('0') << std::hex << std::setw(2) << (int)rpv[34] << std::setw(2) << (int)rpv[35];
	return ss.str();
}

uint32_t bridge_tree::hello_time() const
{
	unsigned short ht;
	STP_GetRootTimes(_parent->stp_bridge(), (unsigned int)_tree_index, nullptr, &ht, nullptr, nullptr, nullptr);
	return ht;
}

uint32_t bridge_tree::max_age() const
{
	unsigned short ma;
	STP_GetRootTimes(_parent->stp_bridge(), (unsigned int)_tree_index, nullptr, nullptr, &ma, nullptr, nullptr);
	return ma;
}

uint32_t bridge_tree::bridge_forward_delay() const
{
	unsigned short fd;
	STP_GetRootTimes(_parent->stp_bridge(), (unsigned int)_tree_index, &fd, nullptr, nullptr, nullptr, nullptr);
	return fd;
}

uint32_t bridge_tree::message_age() const
{
	unsigned short ma;
	STP_GetRootTimes(_parent->stp_bridge(), (unsigned int)_tree_index, nullptr, nullptr, nullptr, &ma, nullptr);
	return ma;
}

uint32_t bridge_tree::remaining_hops() const
{
	unsigned char rh;
	STP_GetRootTimes(_parent->stp_bridge(), (unsigned int)_tree_index, nullptr, nullptr, nullptr, nullptr, &rh);
	return rh;
}

// ============================================================================

static const property_group rpv_group = { 4, "Root Priority Vector" };
static const property_group root_times_group = { 5, "Root Times" };

const nvp bridge_priority_nvps[] =
{
	{ "1000 (4096 dec)", 0x1000 },
	{ "2000 (8192 dec)", 0x2000 },
	{ "3000 (12288 dec)", 0x3000 },
	{ "4000 (16384 dec)", 0x4000 },
	{ "5000 (20480 dec)", 0x5000 },
	{ "6000 (24576 dec)", 0x6000 },
	{ "7000 (28672 dec)", 0x7000 },
	{ "8000 (32768 dec)", 0x8000 },
	{ "9000 (36864 dec)", 0x9000 },
	{ "A000 (40960 dec)", 0xA000 },
	{ "B000 (45056 dec)", 0xB000 },
	{ "C000 (49152 dec)", 0xC000 },
	{ "D000 (53248 dec)", 0xD000 },
	{ "E000 (57344 dec)", 0xE000 },
	{ "F000 (61440 dec)", 0xF000 },
	{ nullptr, 0 },
};

const char bridge_priority_type_name[] = "bridge_priority";

const bridge_priority_p bridge_tree::bridge_priority_property {
	"BridgePriority", nullptr, nullptr, ui_visible::yes,
	static_cast<bridge_priority_p::member_getter_t>(&bridge_priority),
	static_cast<bridge_priority_p::member_setter_t>(&set_bridge_priority),
	0x8000,
};

const temp_string_p bridge_tree::root_id_property =
	{ "RootID", &rpv_group, nullptr, ui_visible::yes, &root_bridge_id, nullptr, };

const uint32_p bridge_tree::external_root_path_cost_property =
	{ "ExternalRootPathCost", &rpv_group, nullptr, ui_visible::yes, &external_root_path_cost, nullptr };

const temp_string_p bridge_tree::regional_root_id_property =
	{ "RegionalRootId", &rpv_group, nullptr, ui_visible::yes, &regional_root_id, nullptr };

const uint32_p bridge_tree::internal_root_path_cost_property =
	{ "InternalRootPathCost", &rpv_group, nullptr, ui_visible::yes, &internal_root_path_cost, nullptr };

const temp_string_p bridge_tree::designated_bridge_id_property =
	{ "DesignatedBridgeId", &rpv_group, nullptr, ui_visible::yes, &designated_bridge_id, nullptr };

const temp_string_p bridge_tree::designated_port_id_property =
	{ "DesignatedPortId", &rpv_group, nullptr, ui_visible::yes, &designated_port_id, nullptr };

const temp_string_p bridge_tree::receiving_port_id_property =
	{ "ReceivingPortId", &rpv_group, nullptr, ui_visible::yes, &receiving_port_id, nullptr };

const edge::uint32_p bridge_tree::hello_time_property =
	{ "HelloTime", &root_times_group, nullptr, ui_visible::yes, &hello_time, nullptr };

const edge::uint32_p bridge_tree::max_age_property =
	{ "MaxAge", &root_times_group, nullptr, ui_visible::yes, &max_age, nullptr };

const edge::uint32_p bridge_tree::forward_delay_property =
	{ "ForwardDelay", &root_times_group, nullptr, ui_visible::yes, &bridge_forward_delay, nullptr };

const edge::uint32_p bridge_tree::message_age_property =
	{ "MessageAge", &root_times_group, nullptr, ui_visible::yes, &message_age, nullptr };

const edge::uint32_p bridge_tree::remaining_hops_property =
	{ "remainingHops", &root_times_group, nullptr, ui_visible::yes, &remaining_hops, nullptr };

const edge::uint32_p bridge_tree::topology_change_count_property =
	{ "Topology Change Count", nullptr, nullptr, ui_visible::yes, &topology_change_count, nullptr };

const edge::property* const bridge_tree::_properties[] =
{
	&bridge_priority_property,
	&root_id_property,
	&topology_change_count_property,
	&external_root_path_cost_property,
	&regional_root_id_property,
	&internal_root_path_cost_property,
	&designated_bridge_id_property,
	&designated_port_id_property,
	&receiving_port_id_property,
	&hello_time_property,
	&max_age_property,
	&forward_delay_property,
	&message_age_property,
	&remaining_hops_property
};

const edge::xtype<bridge_tree> bridge_tree::_type = { "BridgeTree", &base::_type, _properties, nullptr };

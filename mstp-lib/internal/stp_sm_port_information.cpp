
// This file is part of the mstp-lib library, available at https://github.com/adigostin/mstp-lib
// Copyright (c) 2011-2020 Adi Gostin, distributed under Apache License v2.0.

// This file implements 13.35 from 802.1Q-2018.

#include "stp_procedures.h"
#include "stp_conditions_and_params.h"
#include "stp_bridge.h"
#include <assert.h>

using namespace PortInformation;

#if STP_USE_LOG
static const char* GetStateName (State state)
{
	switch (state)
	{
		case DISABLED:				return "DISABLED";
		case AGED:					return "AGED";
		case UPDATE:				return "UPDATE";
		case SUPERIOR_DESIGNATED:	return "SUPERIOR_DESIGNATED";
		case REPEATED_DESIGNATED:	return "REPEATED_DESIGNATED";
		case INFERIOR_DESIGNATED:	return "INFERIOR_DESIGNATED";
		case NOT_DESIGNATED:		return "NOT_DESIGNATED";
		case OTHER:					return "OTHER";
		case CURRENT:				return "CURRENT";
		case RECEIVE:				return "RECEIVE";
		default:					return "(undefined)";
	}
}
#endif

// ============================================================================

// Returns the new state, or 0 when no transition is to be made.
static State CheckConditions (const STP_BRIDGE* bridge, PortAndTree pt, State state)
{
	PortIndex givenPort = pt.portIndex;
	TreeIndex givenTree = pt.treeIndex;

	PORT* port = bridge->ports[givenPort];
	PORT_TREE* portTree = port->trees[givenTree];

	// ------------------------------------------------------------------------
	// Check global conditions.

	if ((!port->portEnabled && (portTree->infoIs != INFO_IS_DISABLED)) || bridge->BEGIN)
	{
		if (state == DISABLED)
		{
			// The entry block for this state has been executed already.
			return (State)0;
		}

		return DISABLED;
	}

	// ------------------------------------------------------------------------
	// Check exit conditions from each state.

	if (state == DISABLED)
	{
		if (portTree->rcvdMsg)
			return DISABLED;

		if (port->portEnabled)
			return AGED;

		return (State)0;
	}

	if (state == AGED)
	{
		if (portTree->selected && portTree->updtInfo)
			return UPDATE;

		return (State)0;
	}

	if (state == UPDATE)
		return CURRENT;

	if (state == SUPERIOR_DESIGNATED)
		return CURRENT;

	if (state == REPEATED_DESIGNATED)
		return CURRENT;

	if (state == INFERIOR_DESIGNATED)
		return CURRENT;

	if (state == NOT_DESIGNATED)
		return CURRENT;

	if (state == OTHER)
		return CURRENT;

	if (state == CURRENT)
	{
		if (portTree->selected && portTree->updtInfo)
			return UPDATE;

		if ((portTree->infoIs == INFO_IS_RECEIVED) && (portTree->rcvdInfoWhile == 0) && !portTree->updtInfo && !rcvdXstMsg (bridge, givenPort, givenTree))
			return AGED;

		if (rcvdXstMsg (bridge, givenPort, givenTree) && !updtXstInfo (bridge, givenPort, givenTree))
			return RECEIVE;

		return (State)0;
	}

	if (state == RECEIVE)
	{
		if (portTree->rcvdInfo == RCVD_INFO_SUPERIOR_DESIGNATED)
			return SUPERIOR_DESIGNATED;

		if (portTree->rcvdInfo == RCVD_INFO_REPEATED_DESIGNATED)
			return REPEATED_DESIGNATED;

		if (portTree->rcvdInfo == RCVD_INFO_INFERIOR_DESIGNATED)
			return INFERIOR_DESIGNATED;

		if (portTree->rcvdInfo == RCVD_INFO_INFERIOR_ROOT_ALTERNATE)
			return NOT_DESIGNATED;

		if (portTree->rcvdInfo == RCVD_INFO_OTHER)
			return OTHER;

		return (State)0;
	}

	assert (false);
	return (State)0;
}

// ============================================================================

static void InitState (STP_BRIDGE* bridge, PortAndTree pt, State state, unsigned int timestamp)
{
	PortIndex givenPort = pt.portIndex;
	TreeIndex givenTree = pt.treeIndex;

	PORT* port = bridge->ports [givenPort];
	PORT_TREE* portTree = port->trees [givenTree];

	if (state == DISABLED)
	{
		portTree->rcvdMsg = false;
		portTree->proposing = portTree->proposed = portTree->agree = portTree->agreed = false;
		portTree->rcvdInfoWhile = 0;
		portTree->infoIs = INFO_IS_DISABLED; portTree->reselect = true; portTree->selected = false;
	}
	else if (state == AGED)
	{
		portTree->infoIs = INFO_IS_AGED;
		portTree->reselect = true;
		portTree->selected = false;
	}
	else if (state == UPDATE)
	{
		portTree->proposing = portTree->proposed = false;
		portTree->agreed = portTree->agreed && betterorsameInfo (bridge, givenPort, givenTree, INFO_IS_MINE);
		portTree->synced = portTree->synced && portTree->agreed;

//LOG (bridge, pi, ti, "-------------------------\r\n");
//LOG (bridge, pi, ti, "{S} portTree->portPriority = portTree->designatedPriority\r\n", port->debugName);
//LOG (bridge, pi, ti, "{S}         old = {PVS}\r\n", port->debugName, &portTree->portPriority);

		portTree->portPriority = portTree->designatedPriority;

//LOG (bridge, pi, ti, "{S}         new = {PVS}\r\n", port->debugName, &portTree->portPriority);
//LOG (bridge, pi, ti, "-------------------------\r\n");

		portTree->portTimes = portTree->designatedTimes;
		portTree->updtInfo = false;
		portTree->infoIs = INFO_IS_MINE;

		if (givenTree == CIST_INDEX)
			port->newInfo = true;
		else
			port->newInfoMsti = true;
	}
	else if (state == SUPERIOR_DESIGNATED)
	{
		port->infoInternal = port->rcvdInternal;
		portTree->agreed = portTree->proposing = false;
		recordProposal (bridge, givenPort, givenTree);
		setTcFlags (bridge, givenPort, givenTree);
		portTree->agree = portTree->agree && betterorsameInfo (bridge, givenPort, givenTree, INFO_IS_RECEIVED);
		recordAgreement (bridge, givenPort, givenTree);
		portTree->synced = portTree->synced && portTree->agreed;
		recordPriority (bridge, givenPort, givenTree);
		recordTimes (bridge, givenPort, givenTree);
		updtRcvdInfoWhile (bridge, givenPort, givenTree);
		portTree->infoIs = INFO_IS_RECEIVED;
		portTree->reselect = true;
		portTree->selected = false;
		portTree->rcvdMsg = false;
	}
	else if (state == REPEATED_DESIGNATED)
	{
		port->infoInternal = port->rcvdInternal;
		recordProposal (bridge, givenPort, givenTree);
		setTcFlags (bridge, givenPort, givenTree);
		recordAgreement (bridge, givenPort, givenTree);
		updtRcvdInfoWhile (bridge, givenPort, givenTree);
		portTree->rcvdMsg = false;
	}
	else if (state == INFERIOR_DESIGNATED)
	{
		recordDispute (bridge, givenPort, givenTree);
		portTree->rcvdMsg = false;
	}
	else if (state == NOT_DESIGNATED)
	{
		recordAgreement (bridge, givenPort, givenTree);
		setTcFlags (bridge, givenPort, givenTree);
		portTree->rcvdMsg = false;
	}
	else if (state == OTHER)
	{
		portTree->rcvdMsg = false;
	}
	else if (state == CURRENT)
	{
	}
	else if (state == RECEIVE)
	{
		portTree->rcvdInfo = rcvInfo (bridge, givenPort, givenTree);
		recordMastered (bridge, givenPort, givenTree);
	}
	else
		assert (false);
}

const StateMachine<State, PortAndTree> PortInformation::sm =
{
#if STP_USE_LOG
	"PortInformation",
	&GetStateName,
#endif
	&CheckConditions,
	&InitState
};

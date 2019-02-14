
// This file is part of the mstp-lib library, available at https://github.com/adigostin/mstp-lib
// Copyright (c) 2011-2019 Adi Gostin, distributed under Apache License v2.0.

#include "stp_procedures.h"
#include "stp_bridge.h"
#include <assert.h>

// See �13.36 in 802.1Q-2011

struct PortStateTransitionImpl : PortStateTransition
{
	static const char* GetStateName (State state)
	{
		switch (state)
		{
			case DISCARDING:	return "DISCARDING";
			case LEARNING:		return "LEARNING";
			case FORWARDING:	return "FORWARDING";
			default:			return "(undefined)";
		}
	}

	// ============================================================================

	// Returns the new state, or 0 when no transition is to be made.
	static State CheckConditions (const STP_BRIDGE* bridge, int givenPort, int givenTree, State state)
	{
		assert (givenPort != -1);
		assert (givenTree != -1);

		PORT* port = bridge->ports [givenPort];
		PORT_TREE* tree = port->trees [givenTree];

		// ------------------------------------------------------------------------
		// Check global conditions.

		if (bridge->BEGIN)
		{
			if (state == DISCARDING)
			{
				// The entry block for this state has been executed already.
				return (State)0;
			}

			return DISCARDING;
		}

		// ------------------------------------------------------------------------
		// Check exit conditions from each state.

		if (state == DISCARDING)
		{
			if (tree->learn)
				return LEARNING;

			return (State)0;
		}

		if (state == LEARNING)
		{
			if (!tree->learn)
				return DISCARDING;

			if (tree->forward)
				return FORWARDING;

			return (State)0;
		}

		if (state == FORWARDING)
		{
			if (!tree->forward)
				return DISCARDING;

			return (State)0;
		}

		assert (false);
		return (State)0;
	}

	// ============================================================================

	static void InitState (STP_BRIDGE* bridge, int givenPort, int givenTree, State state, unsigned int timestamp)
	{
		assert (givenPort != -1);
		assert (givenTree != -1);

		PORT* port = bridge->ports [givenPort];
		PORT_TREE* tree = port->trees [givenTree];

		if (state == DISCARDING)
		{
			disableLearning (bridge, givenPort, givenTree, timestamp);
			tree->learning = false;
			disableForwarding (bridge, givenPort, givenTree, timestamp);
			tree->forwarding = false;
		}
		else if (state == LEARNING)
		{
			enableLearning (bridge, givenPort, givenTree, timestamp);
			tree->learning = true;
		}
		else if (state == FORWARDING)
		{
			enableForwarding (bridge, givenPort, givenTree, timestamp);
			tree->forwarding = true;
		}
		else
			assert (false);
	}
};

const SM_INFO<PortStateTransition::State> PortStateTransition::sm =
{
	"PortStateTransition",
	&PortStateTransitionImpl::GetStateName,
	&PortStateTransitionImpl::CheckConditions,
	&PortStateTransitionImpl::InitState
};
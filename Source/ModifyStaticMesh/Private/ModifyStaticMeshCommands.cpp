// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModifyStaticMeshCommands.h"

#define LOCTEXT_NAMESPACE "FModifyStaticMeshModule"

void FModifyStaticMeshCommands::RegisterCommands()
{
	UI_COMMAND(PluginAction, "ModifyStaticMeshPivot", "Execute ModifyStaticMesh action", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(ReplaceAction, "ReplaceActor", "Execute ReplaceActor action", EUserInterfaceActionType::Button, FInputGesture());
}

#undef LOCTEXT_NAMESPACE

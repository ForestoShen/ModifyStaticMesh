// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "ModifyStaticMeshStyle.h"

class FModifyStaticMeshCommands : public TCommands<FModifyStaticMeshCommands>
{
public:

	FModifyStaticMeshCommands()
		: TCommands<FModifyStaticMeshCommands>(TEXT("ModifyStaticMesh"), NSLOCTEXT("Contexts", "ModifyStaticMesh", "ModifyStaticMesh Plugin"), NAME_None, FModifyStaticMeshStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > PluginAction;
	TSharedPtr< FUICommandInfo > ReplaceAction;
};

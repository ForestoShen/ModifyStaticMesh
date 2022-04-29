// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModifyStaticMesh.h"

#include "ActorGroupingUtils.h"
#include "AssetToolsModule.h"
#include "BuildNetwork.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "MeshDescription.h"
#include "ModifyStaticMeshStyle.h"
#include "ModifyStaticMeshCommands.h"
#include "RawMesh.h"
#include "Misc/MessageDialog.h"
#include "ToolMenus.h"
#include "../../../../TDGBuildingSystem/TDGBuildingSystem/Source/TDGBuildingSystemEditor/Public/BSObjectData.h"
#include "../../../../TDGBuildingSystem/TDGBuildingSystem/Source/TDGBuildingSystemEditor/Public/BuildingExpression/BuildExpression_WallMerge.h"
#include "../../../../TDGBuildingSystem/TDGBuildingSystem/Source/TDGBuildingSystemEditor/Public/BuildingGraph/BuildingGraphObject.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BuildingExpression/BuildExpression_WallGroup.h"
#include "Engine/Selection.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMeshSocket.h"
#include "Kismet/GameplayStatics.h"

static const FName ModifyStaticMeshTabName("ModifyStaticMesh");

#define LOCTEXT_NAMESPACE "FModifyStaticMeshModule"

void FModifyStaticMeshModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FModifyStaticMeshStyle::Initialize();
	FModifyStaticMeshStyle::ReloadTextures();

	FModifyStaticMeshCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FModifyStaticMeshCommands::Get().PluginAction,
		FExecuteAction::CreateRaw(this, &FModifyStaticMeshModule::PluginButtonClicked),
		FCanExecuteAction());

	PluginCommands->MapAction(
		FModifyStaticMeshCommands::Get().ReplaceAction,
		FExecuteAction::CreateRaw(this, &FModifyStaticMeshModule::ReplaceClicked),
		FCanExecuteAction());


	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FModifyStaticMeshModule::RegisterMenus));
}

void FModifyStaticMeshModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FModifyStaticMeshStyle::Shutdown();

	FModifyStaticMeshCommands::Unregister();
}


void ModifyStaticMeshPivot(UStaticMesh* StaticMesh, int XAlign = -1, int YAlign = -1, int ZAlign = -1)
{
	TArray<FStaticMaterial> mats = StaticMesh->StaticMaterials;
	auto remap = StaticMesh->MaterialRemapIndexPerImportVersion;
	for (int i = 0; i < remap.Num(); ++i)
	{
		for (auto ii : remap[i].MaterialRemap)
		{
			UE_LOG(LogTemp, Error, TEXT("remap %i, remap index %i"), i, ii);
		}
	}
	auto SectionInfoMap = StaticMesh->GetSectionInfoMap();
	int num = StaticMesh->GetNumSections(0);

	// 0. Transform setup
	FBox bbox = StaticMesh->GetBoundingBox();
	float Xamount, Yamount, Zamount = 0;
	//UE_LOG(LogTemp, Error, TEXT("FBox min %s, max %s"), *bbox.Min.ToString(),  *bbox.Max.ToString())
	switch(XAlign)
	{
		case 0: Xamount = bbox.Min.X;break;
		case 1: Xamount = bbox.GetCenter().X;break;
		case 2: Xamount = bbox.Max.X;break;
		default: Xamount = 0;
	}
	switch(YAlign)
	{
	case 0: Yamount = bbox.Min.Y;break;
	case 1: Yamount = bbox.GetCenter().Y;break;
	case 2: Yamount = bbox.Max.Y;break;
	default: Yamount = 0;
	}
	switch(ZAlign)
	{
	case 0: Zamount = bbox.Min.Z;break;
	case 1: Zamount = bbox.GetCenter().Z;break;
	case 2: Zamount = bbox.Max.Z;break;
	default: Zamount = 0;
	}
	
	FTransform translation;
	translation.SetLocation(FVector(-Xamount, -Yamount, -Zamount));
	FTransform rotation;
	rotation.SetRotation(FQuat::MakeFromEuler(FVector(0,0,0)));
	FTransform EndTransform = translation * rotation;

	//UE_LOG(LogTemp, Warning, TEXT("Move to location %s"), *EndTransform.GetLocation().ToString())
	
	// 1. Transform raw vertex data.
	auto& SourceModels = StaticMesh->GetSourceModels();
	for (auto& model : SourceModels)
	{
		FRawMesh rawMesh;
		model.LoadRawMesh(rawMesh);
		// Rotate and move pivot
		for (auto& pos : rawMesh.VertexPositions)
		{
			pos = EndTransform.TransformPosition(pos);
		}
		model.SaveRawMesh(rawMesh);
	}
	
	// 2. Transform All Collisions data. ConvexElems, box, etc.
	auto& collisionData = StaticMesh->BodySetup->AggGeom;
	for (auto& convex :collisionData.BoxElems)
	{
		FTransform trans = convex.GetTransform();
		convex.SetTransform(trans * EndTransform);
	}
	for (auto& convex :collisionData.SphereElems)
	{
		FTransform trans = convex.GetTransform();
		convex.SetTransform(trans * EndTransform);
	}
	for (auto& convex :collisionData.SphylElems)
	{
		FTransform trans = convex.GetTransform();
		convex.SetTransform(trans * EndTransform);
	}
	for (auto& convex :collisionData.TaperedCapsuleElems)
	{
		FTransform trans = convex.GetTransform();
		convex.SetTransform(trans * EndTransform);
	}
	StaticMesh->BodySetup->InvalidatePhysicsData();
	for (auto& convex :collisionData.ConvexElems)
	{
		FTransform trans = convex.GetTransform();
		convex.SetTransform(trans * EndTransform);
		convex.BakeTransformToVerts();
	}
	StaticMesh->BodySetup->CreatePhysicsMeshes();

	// 3. Move socket points as well

	for (auto socket : StaticMesh->Sockets)
	{
		socket->RelativeLocation = EndTransform.TransformPosition(socket->RelativeLocation);
		socket->RelativeRotation = EndTransform.TransformRotation(socket->RelativeRotation.Quaternion()).Rotator();
	}
	StaticMesh->Build(false);

	
	// @TODO 3.Fix fx material orderet
	StaticMesh->SectionInfoMap = SectionInfoMap;
	StaticMesh->OriginalSectionInfoMap = SectionInfoMap;
	
	for (int i = 0; i < num; ++i)
	{
		int idx = SectionInfoMap.Map[i].MaterialIndex;
		StaticMesh->SetMaterial(i, mats[idx].MaterialInterface);
	}
			
	/*for (int i = 0; i < mats.Num(); ++i)
	{
		int idx = StaticMesh->GetMaterialIndexFromImportedMaterialSlotName(mats[i].ImportedMaterialSlotName);
		StaticMesh->SetMaterial(idx, mats[i].MaterialInterface);

	}*/
	// Make package dirty.
	StaticMesh->MarkPackageDirty();
	StaticMesh->PostEditChange();
}


void FModifyStaticMeshModule::PluginButtonClicked()
{
	// Put your "OnButtonClicked" stuff here
	FText DialogText = FText::Format(
							LOCTEXT("PluginButtonDialogText", "Add code to {0} in {1} to override this button's actions"),
							FText::FromString(TEXT("FModifyStaticMeshModule::PluginButtonClicked()")),
							FText::FromString(TEXT("ModifyStaticMesh.cpp"))
					   );
	

	TArray<FString> Folders;
	TArray<FAssetData> AllAssets;
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	IContentBrowserSingleton& ContentBrowserSingleton = ContentBrowserModule.Get();
	ContentBrowserSingleton.GetSelectedFolders(Folders);
	ContentBrowserSingleton.GetSelectedAssets(AllAssets);
	
	TArray<FString> AssetsToExport;
	// change module pivot
	for (auto& Folder : Folders)
	{
		// Use AssetRegistry to iterate all staticmesh in selected folder
		FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> allAssets;
		FARFilter filter;
		filter.ClassNames.Add(UStaticMesh::StaticClass()->GetFName());
		filter.bRecursivePaths = true;
		filter.PackagePaths.Add(*Folder);
		AssetRegistry.Get().GetAssets(filter, allAssets);

		for (auto& Asset : allAssets)
		{
			AssetsToExport.Add(Asset.PackageName.ToString());
			UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset.GetAsset());
			ModifyStaticMeshPivot(StaticMesh,0,1,0);
		}
	}

	
	/*
	// change building graph setting
	for (auto& Folder : Folders)
	{
		// Use AssetRegistry to iterate all staticmesh in selected folder
		FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> allAssets;
		FARFilter filter;
		filter.ClassNames.Add(UBuildingGraphObject::StaticClass()->GetFName());
		filter.bRecursivePaths = true;
		filter.PackagePaths.Add(*Folder);
		AssetRegistry.Get().GetAssets(filter, allAssets);

		for (auto& Asset : allAssets)
		{
			UBuildingGraphObject* BGO = Cast<UBuildingGraphObject>(Asset.GetAsset());
			UE_LOG(LogTemp, Error, TEXT("%s is modified"), *BGO->GetName())
			// BGO->ExpressionList is empty... never get used
			TArray<UBuildingSystemGraphNode*> outputs;
			for (auto& grpah : BGO->BuildingGraph->SubGraphs)
			{
				UE_LOG(LogTemp, Error, TEXT("In graph %s"), *grpah->GetName());

				for (UEdGraphNode* node : grpah->Nodes)
				{
					if(node == nullptr)
					{
						continue;
					}
					UBuildingSystemGraphNode* buildingNode = Cast<UBuildingSystemGraphNode>(node);
					if(buildingNode == nullptr)
					{
						continue;
					}
					UBuildExpression_WallGroup* wall  = Cast<UBuildExpression_WallGroup>(buildingNode->BuildSystemExpression);
					if(wall && wall->GroupName.Contains("corner"))
					{
						UE_LOG(LogTemp, Error, TEXT("modify wallgroup %s"), *grpah->GetName());
						wall->GroupComponetWidth = 0;
						wall->GroupComponetLength = 0;
						
					}
					
				}
			}
			
			BGO->BuildingGraph->PostEditChange();
			
			BGO->PreEditChange(nullptr);
			BGO->PostEditChange();
			
		}
	}
	*/

	/*// bulk export changed mesh
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTools.ExportAssetsWithDialog(AssetsToExport, false);*/

	FMessageDialog::Open(EAppMsgType::Ok, DialogText);
}


void FModifyStaticMeshModule::ReplaceClicked()
{
	USelection* SelectedActors = GEditor->GetSelectedActors();
	TArray<AActor*> Actors;
	//TArray<ULevel*> UniqueLevels;

	for (FSelectionIterator SelectionIt(*SelectedActors); SelectionIt; ++SelectionIt)
	{
		AActor* Actor = CastChecked<AActor>(*SelectionIt);
		if (Actor)
		{
			Actors.Add(Actor);
			//UniqueLevels.AddUnique(Actor->GetLevel());
		}
		
		
	}
	auto world = GEditor->GetEditorWorldContext().World();
	auto level = world->GetCurrentLevel();
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(world, AStaticMeshActor::StaticClass(), FoundActors);

	TArray<AActor*> BuildNetworks;
	UGameplayStatics::GetAllActorsOfClass(world, ABuildNetwork::StaticClass(), BuildNetworks);
	for (auto build : BuildNetworks)
	{
		FBox bbox = build->GetComponentsBoundingBox().ExpandBy(10);
		TArray<AActor*> Groups;
		for (auto Actor : FoundActors)
		{
			bool inside = FMath::PointBoxIntersection(Actor->GetActorLocation(), bbox);
			if(inside)
			{
				Groups.Add(Actor);
			}
		}
		if(Groups.Num()>0)
		{
			Groups.Add(build);
			GEditor->GetActorGroupingUtils()->GroupActors(Groups);
		}
	}
	
	FString DialogText = FString::Printf(TEXT("test repalce, actor %s is selected"), *Actors[0]->GetName());
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(DialogText));

	
	
	return;
}
void FModifyStaticMeshModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FModifyStaticMeshCommands::Get().PluginAction, PluginCommands);
		}
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("MarketPlace");
			{
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FModifyStaticMeshCommands::Get().PluginAction));
				Entry.SetCommandList(PluginCommands);
				FToolMenuEntry& Entry2 = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FModifyStaticMeshCommands::Get().ReplaceAction));
				Entry2.SetCommandList(PluginCommands);
			}
		}
	}

}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FModifyStaticMeshModule, ModifyStaticMesh)
// Handlers_SkeletalMesh.cpp
// Skeletal mesh inspection handlers for AgenticMCP.
// Provides visibility into skeletal meshes, bones, morph targets, and sockets.
//
// Endpoints:
//   skelMeshList          - List all skeletal mesh assets
//   skelMeshGetInfo       - Get detailed skeletal mesh info (bones, LODs, morph targets)
//   skelMeshGetBones      - Get the full bone hierarchy
//   skelMeshGetMorphTargets - Get all morph targets and their ranges
//   skelMeshGetSockets    - Get all sockets on a skeletal mesh

#include "AgenticMCPServer.h"
#include "Editor.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "ReferenceSkeleton.h"

// ============================================================
// skelMeshList - List all skeletal mesh assets
// ============================================================
FString FAgenticMCPServer::HandleSkelMeshList(const FString& Body)
{
	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available"));
	}
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> MeshAssets;
	AR.GetAssetsByClass(USkeletalMesh::StaticClass()->GetClassPathName(), MeshAssets, true);

	TSharedPtr<FJsonObject> BodyJson = ParseBodyJson(Body);
	FString Filter;
	if (BodyJson.IsValid() && BodyJson->HasField(TEXT("filter")))
		Filter = BodyJson->GetStringField(TEXT("filter"));

	TArray<TSharedPtr<FJsonValue>> MeshArr;
	for (const FAssetData& Asset : MeshAssets)
	{
		FString Name = Asset.AssetName.ToString();
		if (!Filter.IsEmpty() && !Name.Contains(Filter))
			continue;

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Name);
		Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		MeshArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("count"), MeshArr.Num());
	Result->SetArrayField(TEXT("skeletalMeshes"), MeshArr);
	return JsonToString(Result);
}

// ============================================================
// skelMeshGetInfo - Get detailed skeletal mesh info
// ============================================================
FString FAgenticMCPServer::HandleSkelMeshGetInfo(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Name = Json->GetStringField(TEXT("name"));
	if (Name.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: name"));

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> MeshAssets;
	AR.GetAssetsByClass(USkeletalMesh::StaticClass()->GetClassPathName(), MeshAssets, true);

	USkeletalMesh* Mesh = nullptr;
	for (const FAssetData& Asset : MeshAssets)
	{
		if (Asset.AssetName.ToString() == Name || Asset.GetObjectPathString().Contains(Name))
		{
			Mesh = Cast<USkeletalMesh>(Asset.GetAsset());
			break;
		}
	}
	if (!Mesh) return MakeErrorJson(FString::Printf(TEXT("Skeletal mesh not found: %s"), *Name));

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), Mesh->GetName());
	Result->SetStringField(TEXT("path"), Mesh->GetPathName());

	// Skeleton info
	const FReferenceSkeleton& RefSkel = Mesh->GetRefSkeleton();
	Result->SetNumberField(TEXT("boneCount"), RefSkel.GetNum());

	// LOD info
	FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
	if (RenderData)
	{
		Result->SetNumberField(TEXT("lodCount"), RenderData->LODRenderData.Num());
		if (RenderData->LODRenderData.Num() > 0)
		{
			const FSkeletalMeshLODRenderData& LOD0 = RenderData->LODRenderData[0];
			Result->SetNumberField(TEXT("lod0Vertices"), LOD0.GetNumVertices());
		}
	}

	// Morph targets
	TArray<UMorphTarget*> MorphTargets = Mesh->GetMorphTargets();
	Result->SetNumberField(TEXT("morphTargetCount"), MorphTargets.Num());

	// Sockets
	Result->SetNumberField(TEXT("socketCount"), Mesh->NumSockets());

	// Materials
	TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
	TArray<TSharedPtr<FJsonValue>> MatArr;
	for (const FSkeletalMaterial& Mat : Materials)
	{
		TSharedRef<FJsonObject> MatJson = MakeShared<FJsonObject>();
		MatJson->SetStringField(TEXT("slotName"), Mat.MaterialSlotName.ToString());
		MatJson->SetStringField(TEXT("material"), Mat.MaterialInterface ? Mat.MaterialInterface->GetName() : TEXT("(none)"));
		MatArr.Add(MakeShared<FJsonValueObject>(MatJson));
	}
	Result->SetArrayField(TEXT("materials"), MatArr);

	return JsonToString(Result);
}

// ============================================================
// skelMeshGetBones - Get the full bone hierarchy
// ============================================================
FString FAgenticMCPServer::HandleSkelMeshGetBones(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Name = Json->GetStringField(TEXT("name"));
	if (Name.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: name"));

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> MeshAssets;
	AR.GetAssetsByClass(USkeletalMesh::StaticClass()->GetClassPathName(), MeshAssets, true);

	USkeletalMesh* Mesh = nullptr;
	for (const FAssetData& Asset : MeshAssets)
	{
		if (Asset.AssetName.ToString() == Name || Asset.GetObjectPathString().Contains(Name))
		{
			Mesh = Cast<USkeletalMesh>(Asset.GetAsset());
			break;
		}
	}
	if (!Mesh) return MakeErrorJson(FString::Printf(TEXT("Skeletal mesh not found: %s"), *Name));

	const FReferenceSkeleton& RefSkel = Mesh->GetRefSkeleton();

	TArray<TSharedPtr<FJsonValue>> BoneArr;
	for (int32 i = 0; i < RefSkel.GetNum(); ++i)
	{
		TSharedRef<FJsonObject> BoneJson = MakeShared<FJsonObject>();
		BoneJson->SetNumberField(TEXT("index"), i);
		BoneJson->SetStringField(TEXT("name"), RefSkel.GetBoneName(i).ToString());
		BoneJson->SetNumberField(TEXT("parentIndex"), RefSkel.GetParentIndex(i));

		if (RefSkel.GetParentIndex(i) >= 0)
		{
			BoneJson->SetStringField(TEXT("parentName"), RefSkel.GetBoneName(RefSkel.GetParentIndex(i)).ToString());
		}

		// Reference pose transform
		FTransform BoneTransform = RefSkel.GetRefBonePose()[i];
		FVector Loc = BoneTransform.GetLocation();
		FRotator Rot = BoneTransform.Rotator();
		BoneJson->SetNumberField(TEXT("locationX"), Loc.X);
		BoneJson->SetNumberField(TEXT("locationY"), Loc.Y);
		BoneJson->SetNumberField(TEXT("locationZ"), Loc.Z);
		BoneJson->SetNumberField(TEXT("rotationPitch"), Rot.Pitch);
		BoneJson->SetNumberField(TEXT("rotationYaw"), Rot.Yaw);
		BoneJson->SetNumberField(TEXT("rotationRoll"), Rot.Roll);

		BoneArr.Add(MakeShared<FJsonValueObject>(BoneJson));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("skeletalMesh"), Mesh->GetName());
	Result->SetNumberField(TEXT("boneCount"), BoneArr.Num());
	Result->SetArrayField(TEXT("bones"), BoneArr);
	return JsonToString(Result);
}

// ============================================================
// skelMeshGetMorphTargets - Get all morph targets
// ============================================================
FString FAgenticMCPServer::HandleSkelMeshGetMorphTargets(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Name = Json->GetStringField(TEXT("name"));
	if (Name.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: name"));

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> MeshAssets;
	AR.GetAssetsByClass(USkeletalMesh::StaticClass()->GetClassPathName(), MeshAssets, true);

	USkeletalMesh* Mesh = nullptr;
	for (const FAssetData& Asset : MeshAssets)
	{
		if (Asset.AssetName.ToString() == Name || Asset.GetObjectPathString().Contains(Name))
		{
			Mesh = Cast<USkeletalMesh>(Asset.GetAsset());
			break;
		}
	}
	if (!Mesh) return MakeErrorJson(FString::Printf(TEXT("Skeletal mesh not found: %s"), *Name));

	TArray<UMorphTarget*> MorphTargets = Mesh->GetMorphTargets();

	TArray<TSharedPtr<FJsonValue>> MorphArr;
	for (UMorphTarget* MT : MorphTargets)
	{
		if (!MT) continue;
		TSharedRef<FJsonObject> MTJson = MakeShared<FJsonObject>();
		MTJson->SetStringField(TEXT("name"), MT->GetName());
		MTJson->SetNumberField(TEXT("lodCount"), MT->GetMorphLODModels().Num());

		if (MT->GetMorphLODModels().Num() > 0)
		{
			MTJson->SetNumberField(TEXT("lod0VertexCount"), MT->GetMorphLODModels()[0].Vertices.Num());
		}

		MorphArr.Add(MakeShared<FJsonValueObject>(MTJson));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("skeletalMesh"), Mesh->GetName());
	Result->SetNumberField(TEXT("morphTargetCount"), MorphArr.Num());
	Result->SetArrayField(TEXT("morphTargets"), MorphArr);
	return JsonToString(Result);
}

// ============================================================
// skelMeshGetSockets - Get all sockets
// ============================================================
FString FAgenticMCPServer::HandleSkelMeshGetSockets(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString Name = Json->GetStringField(TEXT("name"));
	if (Name.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: name"));

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> MeshAssets;
	AR.GetAssetsByClass(USkeletalMesh::StaticClass()->GetClassPathName(), MeshAssets, true);

	USkeletalMesh* Mesh = nullptr;
	for (const FAssetData& Asset : MeshAssets)
	{
		if (Asset.AssetName.ToString() == Name || Asset.GetObjectPathString().Contains(Name))
		{
			Mesh = Cast<USkeletalMesh>(Asset.GetAsset());
			break;
		}
	}
	if (!Mesh) return MakeErrorJson(FString::Printf(TEXT("Skeletal mesh not found: %s"), *Name));

	TArray<TSharedPtr<FJsonValue>> SocketArr;
	for (int32 i = 0; i < Mesh->NumSockets(); ++i)
	{
		USkeletalMeshSocket* Socket = Mesh->GetSocketByIndex(i);
		if (!Socket) continue;

		TSharedRef<FJsonObject> SockJson = MakeShared<FJsonObject>();
		SockJson->SetStringField(TEXT("name"), Socket->SocketName.ToString());
		SockJson->SetStringField(TEXT("boneName"), Socket->BoneName.ToString());
		SockJson->SetNumberField(TEXT("locationX"), Socket->RelativeLocation.X);
		SockJson->SetNumberField(TEXT("locationY"), Socket->RelativeLocation.Y);
		SockJson->SetNumberField(TEXT("locationZ"), Socket->RelativeLocation.Z);
		SockJson->SetNumberField(TEXT("rotationPitch"), Socket->RelativeRotation.Pitch);
		SockJson->SetNumberField(TEXT("rotationYaw"), Socket->RelativeRotation.Yaw);
		SockJson->SetNumberField(TEXT("rotationRoll"), Socket->RelativeRotation.Roll);
		SockJson->SetNumberField(TEXT("scale"), Socket->RelativeScale.X);

		SocketArr.Add(MakeShared<FJsonValueObject>(SockJson));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("skeletalMesh"), Mesh->GetName());
	Result->SetNumberField(TEXT("socketCount"), SocketArr.Num());
	Result->SetArrayField(TEXT("sockets"), SocketArr);
	return JsonToString(Result);
}

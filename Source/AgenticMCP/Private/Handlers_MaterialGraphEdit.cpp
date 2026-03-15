// Handlers_MaterialGraphEdit.cpp
// Material graph editing handlers for AgenticMCP.
// Target: UE 5.4 - 5.6
//
// Endpoints:
//   materialAddNode         - Add a material expression node to a material graph
//   materialConnectPins     - Connect two material expression outputs/inputs
//   materialDisconnectPin   - Disconnect a material expression pin
//   materialSetTexture      - Set a texture parameter on a material instance
//   materialSetScalar       - Set a scalar parameter on a material instance
//   materialSetVector       - Set a vector parameter on a material instance
//   materialAssignToActor   - Assign a material to an actor's mesh component
//   materialGetGraph        - Get the node graph of a material

#include "AgenticMCPServer.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

// Helper: find a material by name from asset registry
static UMaterial* FindMaterialByName(const FString& Name)
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> MatAssets;
	AR.GetAssetsByClass(UMaterial::StaticClass()->GetClassPathName(), MatAssets, true);

	for (const FAssetData& Asset : MatAssets)
	{
		if (Asset.AssetName.ToString() == Name || Asset.GetObjectPathString().Contains(Name))
		{
			return Cast<UMaterial>(Asset.GetAsset());
		}
	}
	return nullptr;
}

// Helper: find a material instance by name
static UMaterialInstanceConstant* FindMaterialInstanceByName(const FString& Name)
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> MICAssets;
	AR.GetAssetsByClass(UMaterialInstanceConstant::StaticClass()->GetClassPathName(), MICAssets, true);

	for (const FAssetData& Asset : MICAssets)
	{
		if (Asset.AssetName.ToString() == Name || Asset.GetObjectPathString().Contains(Name))
		{
			return Cast<UMaterialInstanceConstant>(Asset.GetAsset());
		}
	}
	return nullptr;
}

// Helper: find any material interface by name
static UMaterialInterface* FindMaterialInterfaceByName(const FString& Name)
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> MatAssets;
	AR.GetAssetsByClass(UMaterialInterface::StaticClass()->GetClassPathName(), MatAssets, true);

	for (const FAssetData& Asset : MatAssets)
	{
		if (Asset.AssetName.ToString() == Name || Asset.GetObjectPathString().Contains(Name))
		{
			return Cast<UMaterialInterface>(Asset.GetAsset());
		}
	}
	return nullptr;
}

// ============================================================
// materialAddNode - Add a material expression node
// Params: materialName, nodeType, posX (opt), posY (opt),
//         parameterName (opt, for parameter nodes),
//         defaultValue (opt), texturePath (opt)
// ============================================================
FString FAgenticMCPServer::HandleMaterialAddNode(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString MatName = Json->GetStringField(TEXT("materialName"));
	FString NodeType = Json->GetStringField(TEXT("nodeType"));

	if (MatName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: materialName"));
	if (NodeType.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: nodeType"));

	int32 PosX = Json->HasField(TEXT("posX")) ? (int32)Json->GetNumberField(TEXT("posX")) : -300;
	int32 PosY = Json->HasField(TEXT("posY")) ? (int32)Json->GetNumberField(TEXT("posY")) : 0;

	UMaterial* Material = FindMaterialByName(MatName);
	if (!Material) return MakeErrorJson(FString::Printf(TEXT("Material not found: %s (must be a base Material, not an instance)"), *MatName));

	UMaterialExpression* NewExpr = nullptr;

	// Create the expression based on nodeType
	if (NodeType == TEXT("TextureSample"))
	{
		UMaterialExpressionTextureSample* Expr = NewObject<UMaterialExpressionTextureSample>(Material);
		// Optionally set texture
		FString TexPath = Json->GetStringField(TEXT("texturePath"));
		if (!TexPath.IsEmpty())
		{
			UTexture* Tex = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr, *TexPath));
			if (Tex) Expr->Texture = Tex;
		}
		NewExpr = Expr;
	}
	else if (NodeType == TEXT("ScalarParameter"))
	{
		UMaterialExpressionScalarParameter* Expr = NewObject<UMaterialExpressionScalarParameter>(Material);
		FString ParamName = Json->HasField(TEXT("parameterName")) ? Json->GetStringField(TEXT("parameterName")) : TEXT("Scalar");
		Expr->ParameterName = FName(*ParamName);
		if (Json->HasField(TEXT("defaultValue")))
			Expr->DefaultValue = (float)Json->GetNumberField(TEXT("defaultValue"));
		NewExpr = Expr;
	}
	else if (NodeType == TEXT("VectorParameter"))
	{
		UMaterialExpressionVectorParameter* Expr = NewObject<UMaterialExpressionVectorParameter>(Material);
		FString ParamName = Json->HasField(TEXT("parameterName")) ? Json->GetStringField(TEXT("parameterName")) : TEXT("Color");
		Expr->ParameterName = FName(*ParamName);
		if (Json->HasField(TEXT("defaultValue")))
		{
			const TSharedPtr<FJsonObject>* ValObj;
			if (Json->TryGetObjectField(TEXT("defaultValue"), ValObj))
			{
				float R = (*ValObj)->HasField(TEXT("r")) ? (float)(*ValObj)->GetNumberField(TEXT("r")) : 0.f;
				float G = (*ValObj)->HasField(TEXT("g")) ? (float)(*ValObj)->GetNumberField(TEXT("g")) : 0.f;
				float B = (*ValObj)->HasField(TEXT("b")) ? (float)(*ValObj)->GetNumberField(TEXT("b")) : 0.f;
				float A = (*ValObj)->HasField(TEXT("a")) ? (float)(*ValObj)->GetNumberField(TEXT("a")) : 1.f;
				Expr->DefaultValue = FLinearColor(R, G, B, A);
			}
		}
		NewExpr = Expr;
	}
	else if (NodeType == TEXT("TextureCoordinate"))
	{
		UMaterialExpressionTextureCoordinate* Expr = NewObject<UMaterialExpressionTextureCoordinate>(Material);
		if (Json->HasField(TEXT("coordinateIndex")))
			Expr->CoordinateIndex = (int32)Json->GetNumberField(TEXT("coordinateIndex"));
		NewExpr = Expr;
	}
	else if (NodeType == TEXT("Add"))
	{
		NewExpr = NewObject<UMaterialExpressionAdd>(Material);
	}
	else if (NodeType == TEXT("Multiply"))
	{
		NewExpr = NewObject<UMaterialExpressionMultiply>(Material);
	}
	else if (NodeType == TEXT("Lerp"))
	{
		NewExpr = NewObject<UMaterialExpressionLinearInterpolate>(Material);
	}
	else if (NodeType == TEXT("Constant"))
	{
		UMaterialExpressionConstant* Expr = NewObject<UMaterialExpressionConstant>(Material);
		if (Json->HasField(TEXT("defaultValue")))
			Expr->R = (float)Json->GetNumberField(TEXT("defaultValue"));
		NewExpr = Expr;
	}
	else if (NodeType == TEXT("Constant3Vector"))
	{
		UMaterialExpressionConstant3Vector* Expr = NewObject<UMaterialExpressionConstant3Vector>(Material);
		if (Json->HasField(TEXT("defaultValue")))
		{
			const TSharedPtr<FJsonObject>* ValObj;
			if (Json->TryGetObjectField(TEXT("defaultValue"), ValObj))
			{
				float R = (*ValObj)->HasField(TEXT("r")) ? (float)(*ValObj)->GetNumberField(TEXT("r")) : 0.f;
				float G = (*ValObj)->HasField(TEXT("g")) ? (float)(*ValObj)->GetNumberField(TEXT("g")) : 0.f;
				float B = (*ValObj)->HasField(TEXT("b")) ? (float)(*ValObj)->GetNumberField(TEXT("b")) : 0.f;
				Expr->Constant = FLinearColor(R, G, B, 1.f);
			}
		}
		NewExpr = Expr;
	}
	else if (NodeType == TEXT("Fresnel"))
	{
		NewExpr = NewObject<UMaterialExpressionFresnel>(Material);
	}
	else if (NodeType == TEXT("Panner"))
	{
		NewExpr = NewObject<UMaterialExpressionPanner>(Material);
	}
	else if (NodeType == TEXT("Time"))
	{
		NewExpr = NewObject<UMaterialExpressionTime>(Material);
	}
	else if (NodeType == TEXT("OneMinus"))
	{
		NewExpr = NewObject<UMaterialExpressionOneMinus>(Material);
	}
	else if (NodeType == TEXT("Power"))
	{
		NewExpr = NewObject<UMaterialExpressionPower>(Material);
	}
	else if (NodeType == TEXT("Clamp"))
	{
		NewExpr = NewObject<UMaterialExpressionClamp>(Material);
	}
	else if (NodeType == TEXT("Normalize"))
	{
		NewExpr = NewObject<UMaterialExpressionNormalize>(Material);
	}
	else
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Unsupported nodeType: %s. Supported: TextureSample, ScalarParameter, VectorParameter, "
				 "TextureCoordinate, Add, Multiply, Lerp, Constant, Constant3Vector, Fresnel, Panner, "
				 "Time, OneMinus, Power, Clamp, Normalize"),
			*NodeType));
	}

	if (!NewExpr)
		return MakeErrorJson(TEXT("Failed to create material expression"));

	NewExpr->MaterialExpressionEditorX = PosX;
	NewExpr->MaterialExpressionEditorY = PosY;

	Material->GetExpressionCollection().AddExpression(NewExpr);
	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Material->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("materialName"), MatName);
	Result->SetStringField(TEXT("nodeType"), NodeType);
	Result->SetStringField(TEXT("expressionClass"), NewExpr->GetClass()->GetName());
	Result->SetNumberField(TEXT("nodeIndex"), Material->GetExpressionCollection().Expressions.Num() - 1);
	Result->SetNumberField(TEXT("posX"), PosX);
	Result->SetNumberField(TEXT("posY"), PosY);
	return JsonToString(Result);
}

// ============================================================
// materialConnectPins - Connect material expression output to input
// Params: materialName, sourceNodeIndex, sourceOutputIndex (default 0),
//         targetNodeIndex or targetInput (e.g. "BaseColor", "Metallic"),
//         targetInputIndex (default 0, used when targetNodeIndex given)
// ============================================================
FString FAgenticMCPServer::HandleMaterialConnectPins(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString MatName = Json->GetStringField(TEXT("materialName"));
	if (MatName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: materialName"));
	if (!Json->HasField(TEXT("sourceNodeIndex"))) return MakeErrorJson(TEXT("Missing required field: sourceNodeIndex"));

	int32 SrcIdx = (int32)Json->GetNumberField(TEXT("sourceNodeIndex"));
	int32 SrcOutputIdx = Json->HasField(TEXT("sourceOutputIndex")) ? (int32)Json->GetNumberField(TEXT("sourceOutputIndex")) : 0;

	UMaterial* Material = FindMaterialByName(MatName);
	if (!Material) return MakeErrorJson(FString::Printf(TEXT("Material not found: %s"), *MatName));

	const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressionCollection().Expressions;
	if (SrcIdx < 0 || SrcIdx >= Expressions.Num())
		return MakeErrorJson(FString::Printf(TEXT("Source node index %d out of range (0-%d)"), SrcIdx, Expressions.Num() - 1));

	UMaterialExpression* SrcExpr = Expressions[SrcIdx];

	// Check if connecting to a material output pin (BaseColor, Metallic, etc.)
	FString TargetInput = Json->GetStringField(TEXT("targetInput"));
	if (!TargetInput.IsEmpty())
	{
		// Connect to material output
		FExpressionOutput& Output = SrcExpr->GetOutputs()[FMath::Clamp(SrcOutputIdx, 0, SrcExpr->GetOutputs().Num() - 1)];

		bool bConnected = false;
		if (TargetInput == TEXT("BaseColor"))
		{
			Material->GetEditorOnlyData()->BaseColor.Connect(SrcOutputIdx, SrcExpr);
			bConnected = true;
		}
		else if (TargetInput == TEXT("Metallic"))
		{
			Material->GetEditorOnlyData()->Metallic.Connect(SrcOutputIdx, SrcExpr);
			bConnected = true;
		}
		else if (TargetInput == TEXT("Specular"))
		{
			Material->GetEditorOnlyData()->Specular.Connect(SrcOutputIdx, SrcExpr);
			bConnected = true;
		}
		else if (TargetInput == TEXT("Roughness"))
		{
			Material->GetEditorOnlyData()->Roughness.Connect(SrcOutputIdx, SrcExpr);
			bConnected = true;
		}
		else if (TargetInput == TEXT("EmissiveColor"))
		{
			Material->GetEditorOnlyData()->EmissiveColor.Connect(SrcOutputIdx, SrcExpr);
			bConnected = true;
		}
		else if (TargetInput == TEXT("Normal"))
		{
			Material->GetEditorOnlyData()->Normal.Connect(SrcOutputIdx, SrcExpr);
			bConnected = true;
		}
		else if (TargetInput == TEXT("Opacity"))
		{
			Material->GetEditorOnlyData()->Opacity.Connect(SrcOutputIdx, SrcExpr);
			bConnected = true;
		}
		else if (TargetInput == TEXT("OpacityMask"))
		{
			Material->GetEditorOnlyData()->OpacityMask.Connect(SrcOutputIdx, SrcExpr);
			bConnected = true;
		}
		else if (TargetInput == TEXT("AmbientOcclusion"))
		{
			Material->GetEditorOnlyData()->AmbientOcclusion.Connect(SrcOutputIdx, SrcExpr);
			bConnected = true;
		}
		else if (TargetInput == TEXT("WorldPositionOffset"))
		{
			Material->GetEditorOnlyData()->WorldPositionOffset.Connect(SrcOutputIdx, SrcExpr);
			bConnected = true;
		}
		else if (TargetInput == TEXT("SubsurfaceColor"))
		{
			Material->GetEditorOnlyData()->SubsurfaceColor.Connect(SrcOutputIdx, SrcExpr);
			bConnected = true;
		}

		if (!bConnected)
			return MakeErrorJson(FString::Printf(
				TEXT("Unknown targetInput: %s. Supported: BaseColor, Metallic, Specular, Roughness, "
					 "EmissiveColor, Normal, Opacity, OpacityMask, AmbientOcclusion, WorldPositionOffset, SubsurfaceColor"),
				*TargetInput));

		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		Material->MarkPackageDirty();

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("materialName"), MatName);
		Result->SetNumberField(TEXT("sourceNodeIndex"), SrcIdx);
		Result->SetStringField(TEXT("targetInput"), TargetInput);
		return JsonToString(Result);
	}

	// Connect to another expression node's input
	if (!Json->HasField(TEXT("targetNodeIndex")))
		return MakeErrorJson(TEXT("Provide either 'targetInput' (material output) or 'targetNodeIndex' (expression input)"));

	int32 TgtIdx = (int32)Json->GetNumberField(TEXT("targetNodeIndex"));
	int32 TgtInputIdx = Json->HasField(TEXT("targetInputIndex")) ? (int32)Json->GetNumberField(TEXT("targetInputIndex")) : 0;

	if (TgtIdx < 0 || TgtIdx >= Expressions.Num())
		return MakeErrorJson(FString::Printf(TEXT("Target node index %d out of range (0-%d)"), TgtIdx, Expressions.Num() - 1));

	UMaterialExpression* TgtExpr = Expressions[TgtIdx];
	TArray<FExpressionInput*> Inputs = TgtExpr->GetInputs();
	if (TgtInputIdx < 0 || TgtInputIdx >= Inputs.Num())
		return MakeErrorJson(FString::Printf(TEXT("Target input index %d out of range (0-%d)"), TgtInputIdx, Inputs.Num() - 1));

	Inputs[TgtInputIdx]->Connect(SrcOutputIdx, SrcExpr);

	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Material->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("materialName"), MatName);
	Result->SetNumberField(TEXT("sourceNodeIndex"), SrcIdx);
	Result->SetNumberField(TEXT("sourceOutputIndex"), SrcOutputIdx);
	Result->SetNumberField(TEXT("targetNodeIndex"), TgtIdx);
	Result->SetNumberField(TEXT("targetInputIndex"), TgtInputIdx);
	return JsonToString(Result);
}

// ============================================================
// materialDisconnectPin - Disconnect a material expression pin
// Params: materialName, nodeIndex, inputIndex,
//         or targetInput (for material output pins)
// ============================================================
FString FAgenticMCPServer::HandleMaterialDisconnectPin(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString MatName = Json->GetStringField(TEXT("materialName"));
	if (MatName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: materialName"));

	UMaterial* Material = FindMaterialByName(MatName);
	if (!Material) return MakeErrorJson(FString::Printf(TEXT("Material not found: %s"), *MatName));

	// Disconnect a material output pin
	FString TargetInput = Json->GetStringField(TEXT("targetInput"));
	if (!TargetInput.IsEmpty())
	{
		bool bDisconnected = false;
		if (TargetInput == TEXT("BaseColor")) { Material->GetEditorOnlyData()->BaseColor.Expression = nullptr; bDisconnected = true; }
		else if (TargetInput == TEXT("Metallic")) { Material->GetEditorOnlyData()->Metallic.Expression = nullptr; bDisconnected = true; }
		else if (TargetInput == TEXT("Specular")) { Material->GetEditorOnlyData()->Specular.Expression = nullptr; bDisconnected = true; }
		else if (TargetInput == TEXT("Roughness")) { Material->GetEditorOnlyData()->Roughness.Expression = nullptr; bDisconnected = true; }
		else if (TargetInput == TEXT("EmissiveColor")) { Material->GetEditorOnlyData()->EmissiveColor.Expression = nullptr; bDisconnected = true; }
		else if (TargetInput == TEXT("Normal")) { Material->GetEditorOnlyData()->Normal.Expression = nullptr; bDisconnected = true; }
		else if (TargetInput == TEXT("Opacity")) { Material->GetEditorOnlyData()->Opacity.Expression = nullptr; bDisconnected = true; }
		else if (TargetInput == TEXT("OpacityMask")) { Material->GetEditorOnlyData()->OpacityMask.Expression = nullptr; bDisconnected = true; }
		else if (TargetInput == TEXT("AmbientOcclusion")) { Material->GetEditorOnlyData()->AmbientOcclusion.Expression = nullptr; bDisconnected = true; }

		if (!bDisconnected)
			return MakeErrorJson(FString::Printf(TEXT("Unknown targetInput: %s"), *TargetInput));

		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		Material->MarkPackageDirty();

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("materialName"), MatName);
		Result->SetStringField(TEXT("disconnectedInput"), TargetInput);
		return JsonToString(Result);
	}

	// Disconnect an expression node's input
	if (!Json->HasField(TEXT("nodeIndex"))) return MakeErrorJson(TEXT("Provide 'targetInput' or 'nodeIndex'"));
	int32 NodeIdx = (int32)Json->GetNumberField(TEXT("nodeIndex"));
	int32 InputIdx = Json->HasField(TEXT("inputIndex")) ? (int32)Json->GetNumberField(TEXT("inputIndex")) : 0;

	const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressionCollection().Expressions;
	if (NodeIdx < 0 || NodeIdx >= Expressions.Num())
		return MakeErrorJson(FString::Printf(TEXT("Node index %d out of range"), NodeIdx));

	TArray<FExpressionInput*> Inputs = Expressions[NodeIdx]->GetInputs();
	if (InputIdx < 0 || InputIdx >= Inputs.Num())
		return MakeErrorJson(FString::Printf(TEXT("Input index %d out of range"), InputIdx));

	Inputs[InputIdx]->Expression = nullptr;

	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Material->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("materialName"), MatName);
	Result->SetNumberField(TEXT("nodeIndex"), NodeIdx);
	Result->SetNumberField(TEXT("inputIndex"), InputIdx);
	return JsonToString(Result);
}

// ============================================================
// materialSetTexture - Set a texture parameter on a material instance
// Params: materialName, parameterName, texturePath
// ============================================================
FString FAgenticMCPServer::HandleMaterialSetTextureParam(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString MatName = Json->GetStringField(TEXT("materialName"));
	FString ParamName = Json->GetStringField(TEXT("parameterName"));
	FString TexPath = Json->GetStringField(TEXT("texturePath"));

	if (MatName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: materialName"));
	if (ParamName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: parameterName"));
	if (TexPath.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: texturePath"));

	UMaterialInstanceConstant* MIC = FindMaterialInstanceByName(MatName);
	if (!MIC) return MakeErrorJson(FString::Printf(TEXT("Material instance not found: %s"), *MatName));

	UTexture* Tex = Cast<UTexture>(StaticLoadObject(UTexture::StaticClass(), nullptr, *TexPath));
	if (!Tex) return MakeErrorJson(FString::Printf(TEXT("Texture not found: %s"), *TexPath));

	MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(FName(*ParamName)), Tex);
	MIC->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("materialName"), MatName);
	Result->SetStringField(TEXT("parameterName"), ParamName);
	Result->SetStringField(TEXT("textureName"), Tex->GetName());
	return JsonToString(Result);
}

// ============================================================
// materialSetScalar - Set a scalar parameter on a material instance
// Params: materialName, parameterName, value
// ============================================================
FString FAgenticMCPServer::HandleMaterialSetScalar(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString MatName = Json->GetStringField(TEXT("materialName"));
	FString ParamName = Json->GetStringField(TEXT("parameterName"));

	if (MatName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: materialName"));
	if (ParamName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: parameterName"));
	if (!Json->HasField(TEXT("value"))) return MakeErrorJson(TEXT("Missing required field: value"));

	float Value = (float)Json->GetNumberField(TEXT("value"));

	UMaterialInstanceConstant* MIC = FindMaterialInstanceByName(MatName);
	if (!MIC) return MakeErrorJson(FString::Printf(TEXT("Material instance not found: %s"), *MatName));

	MIC->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(FName(*ParamName)), Value);
	MIC->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("materialName"), MatName);
	Result->SetStringField(TEXT("parameterName"), ParamName);
	Result->SetNumberField(TEXT("value"), Value);
	return JsonToString(Result);
}

// ============================================================
// materialSetVector - Set a vector parameter on a material instance
// Params: materialName, parameterName, r, g, b, a (default 1.0)
// ============================================================
FString FAgenticMCPServer::HandleMaterialSetVector(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString MatName = Json->GetStringField(TEXT("materialName"));
	FString ParamName = Json->GetStringField(TEXT("parameterName"));

	if (MatName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: materialName"));
	if (ParamName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: parameterName"));

	float R = Json->HasField(TEXT("r")) ? (float)Json->GetNumberField(TEXT("r")) : 0.f;
	float G = Json->HasField(TEXT("g")) ? (float)Json->GetNumberField(TEXT("g")) : 0.f;
	float B = Json->HasField(TEXT("b")) ? (float)Json->GetNumberField(TEXT("b")) : 0.f;
	float A = Json->HasField(TEXT("a")) ? (float)Json->GetNumberField(TEXT("a")) : 1.f;

	UMaterialInstanceConstant* MIC = FindMaterialInstanceByName(MatName);
	if (!MIC) return MakeErrorJson(FString::Printf(TEXT("Material instance not found: %s"), *MatName));

	MIC->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(FName(*ParamName)), FLinearColor(R, G, B, A));
	MIC->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("materialName"), MatName);
	Result->SetStringField(TEXT("parameterName"), ParamName);
	Result->SetNumberField(TEXT("r"), R);
	Result->SetNumberField(TEXT("g"), G);
	Result->SetNumberField(TEXT("b"), B);
	Result->SetNumberField(TEXT("a"), A);
	return JsonToString(Result);
}

// ============================================================
// materialAssignToActor - Assign a material to an actor's mesh
// Params: actorName, materialName, slotIndex (default 0)
// ============================================================
FString FAgenticMCPServer::HandleMaterialAssignToActor(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ActorName = Json->GetStringField(TEXT("actorName"));
	FString MatName = Json->GetStringField(TEXT("materialName"));

	if (ActorName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: actorName"));
	if (MatName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: materialName"));

	int32 SlotIndex = Json->HasField(TEXT("slotIndex")) ? (int32)Json->GetNumberField(TEXT("slotIndex")) : 0;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return MakeErrorJson(TEXT("No editor world available"));

	// Find actor
	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == ActorName || (*It)->GetName() == ActorName)
		{
			Actor = *It;
			break;
		}
	}
	if (!Actor) return MakeErrorJson(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

	// Find material
	UMaterialInterface* MatInterface = FindMaterialInterfaceByName(MatName);
	if (!MatInterface) return MakeErrorJson(FString::Printf(TEXT("Material not found: %s"), *MatName));

	// Find mesh component
	UMeshComponent* MeshComp = Actor->FindComponentByClass<UStaticMeshComponent>();
	if (!MeshComp) MeshComp = Actor->FindComponentByClass<USkeletalMeshComponent>();
	if (!MeshComp) return MakeErrorJson(TEXT("Actor has no mesh component"));

	if (SlotIndex >= MeshComp->GetNumMaterials())
		return MakeErrorJson(FString::Printf(TEXT("Slot index %d out of range (0-%d)"), SlotIndex, MeshComp->GetNumMaterials() - 1));

	MeshComp->SetMaterial(SlotIndex, MatInterface);
	Actor->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actorName"), ActorName);
	Result->SetStringField(TEXT("materialName"), MatName);
	Result->SetNumberField(TEXT("slotIndex"), SlotIndex);
	Result->SetNumberField(TEXT("totalSlots"), MeshComp->GetNumMaterials());
	return JsonToString(Result);
}

// ============================================================
// materialGetGraph - Get the node graph of a material
// Params: materialName
// ============================================================
FString FAgenticMCPServer::HandleMaterialGetGraph(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString MatName = Json->GetStringField(TEXT("materialName"));
	if (MatName.IsEmpty()) return MakeErrorJson(TEXT("Missing required field: materialName"));

	UMaterial* Material = FindMaterialByName(MatName);
	if (!Material) return MakeErrorJson(FString::Printf(TEXT("Material not found: %s"), *MatName));

	const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressionCollection().Expressions;

	TArray<TSharedPtr<FJsonValue>> NodesArr;
	for (int32 i = 0; i < Expressions.Num(); ++i)
	{
		UMaterialExpression* Expr = Expressions[i];
		if (!Expr) continue;

		TSharedRef<FJsonObject> NodeJson = MakeShared<FJsonObject>();
		NodeJson->SetNumberField(TEXT("index"), i);
		NodeJson->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
		NodeJson->SetStringField(TEXT("description"), Expr->GetDescription());
		NodeJson->SetNumberField(TEXT("posX"), Expr->MaterialExpressionEditorX);
		NodeJson->SetNumberField(TEXT("posY"), Expr->MaterialExpressionEditorY);

		// Parameter name if applicable
		if (UMaterialExpressionParameter* ParamExpr = Cast<UMaterialExpressionParameter>(Expr))
		{
			NodeJson->SetStringField(TEXT("parameterName"), ParamExpr->ParameterName.ToString());
		}

		// Output count
		NodeJson->SetNumberField(TEXT("outputCount"), Expr->GetOutputs().Num());

		// Input connections
		TArray<TSharedPtr<FJsonValue>> InputsArr;
		TArray<FExpressionInput*> Inputs = Expr->GetInputs();
		for (int32 j = 0; j < Inputs.Num(); ++j)
		{
			TSharedRef<FJsonObject> InputJson = MakeShared<FJsonObject>();
			InputJson->SetNumberField(TEXT("inputIndex"), j);
			if (Inputs[j]->Expression)
			{
				int32 ConnectedIdx = Expressions.Find(Inputs[j]->Expression);
				InputJson->SetNumberField(TEXT("connectedNodeIndex"), ConnectedIdx);
				InputJson->SetNumberField(TEXT("connectedOutputIndex"), Inputs[j]->OutputIndex);
			}
			else
			{
				InputJson->SetStringField(TEXT("connected"), TEXT("none"));
			}
			InputsArr.Add(MakeShared<FJsonValueObject>(InputJson));
		}
		NodeJson->SetArrayField(TEXT("inputs"), InputsArr);

		NodesArr.Add(MakeShared<FJsonValueObject>(NodeJson));
	}

	// Material output connections
	TSharedRef<FJsonObject> OutputsJson = MakeShared<FJsonObject>();
	auto ReportConnection = [&](const TCHAR* Name, const FExpressionInput& Input)
	{
		if (Input.Expression)
		{
			int32 Idx = Expressions.Find(Input.Expression);
			TSharedRef<FJsonObject> ConnJson = MakeShared<FJsonObject>();
			ConnJson->SetNumberField(TEXT("nodeIndex"), Idx);
			ConnJson->SetNumberField(TEXT("outputIndex"), Input.OutputIndex);
			OutputsJson->SetObjectField(Name, ConnJson);
		}
	};

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (EditorData)
	{
		ReportConnection(TEXT("BaseColor"), EditorData->BaseColor);
		ReportConnection(TEXT("Metallic"), EditorData->Metallic);
		ReportConnection(TEXT("Specular"), EditorData->Specular);
		ReportConnection(TEXT("Roughness"), EditorData->Roughness);
		ReportConnection(TEXT("EmissiveColor"), EditorData->EmissiveColor);
		ReportConnection(TEXT("Normal"), EditorData->Normal);
		ReportConnection(TEXT("Opacity"), EditorData->Opacity);
		ReportConnection(TEXT("OpacityMask"), EditorData->OpacityMask);
		ReportConnection(TEXT("AmbientOcclusion"), EditorData->AmbientOcclusion);
		ReportConnection(TEXT("WorldPositionOffset"), EditorData->WorldPositionOffset);
		ReportConnection(TEXT("SubsurfaceColor"), EditorData->SubsurfaceColor);
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("materialName"), MatName);
	Result->SetNumberField(TEXT("nodeCount"), NodesArr.Num());
	Result->SetArrayField(TEXT("nodes"), NodesArr);
	Result->SetObjectField(TEXT("materialOutputConnections"), OutputsJson);
	return JsonToString(Result);
}

// ============================================================
// materialCreateInstance - Create a material instance from a parent material
// ============================================================
FString FAgenticMCPServer::HandleMaterialCreateInstance(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString ParentName = Json->GetStringField(TEXT("parentMaterial"));
	if (ParentName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: parentMaterial"));

	FString InstanceName = Json->GetStringField(TEXT("instanceName"));
	if (InstanceName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: instanceName"));

	FString InstancePath = Json->GetStringField(TEXT("path"));
	if (InstancePath.IsEmpty())
		InstancePath = TEXT("/Game/Materials/");

	// Find parent material
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> MatAssets;
	AssetRegistry.GetAssetsByClass(UMaterialInterface::StaticClass()->GetClassPathName(), MatAssets, true);

	UMaterialInterface* ParentMat = nullptr;
	for (const FAssetData& Asset : MatAssets)
	{
		if (Asset.AssetName.ToString() == ParentName || Asset.GetObjectPathString().Contains(ParentName))
		{
			ParentMat = Cast<UMaterialInterface>(Asset.GetAsset());
			break;
		}
	}

	if (!ParentMat)
		return MakeErrorJson(FString::Printf(TEXT("Parent material not found: %s"), *ParentName));

	// Create the material instance constant
	FString PackagePath = InstancePath / InstanceName;
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
		return MakeErrorJson(TEXT("Failed to create package for material instance"));

	UMaterialInstanceConstant* MIC = NewObject<UMaterialInstanceConstant>(
		Package, *InstanceName, RF_Public | RF_Standalone);

	if (!MIC)
		return MakeErrorJson(TEXT("Failed to create material instance"));

	MIC->SetParentEditorOnly(ParentMat);
	MIC->PostEditChange();
	MIC->MarkPackageDirty();

	// Notify asset registry
	FAssetRegistryModule::AssetCreated(MIC);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("instanceName"), MIC->GetName());
	Result->SetStringField(TEXT("instancePath"), MIC->GetPathName());
	Result->SetStringField(TEXT("parentMaterial"), ParentMat->GetName());
	return JsonToString(Result);
}

// ============================================================
// materialDeleteNode - Delete a material expression node from a material
// ============================================================
FString FAgenticMCPServer::HandleMaterialDeleteNode(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid()) return MakeErrorJson(TEXT("Invalid JSON body"));

	FString MaterialName = Json->GetStringField(TEXT("materialName"));
	if (MaterialName.IsEmpty())
		return MakeErrorJson(TEXT("Missing required field: materialName"));

	FString NodeName = Json->GetStringField(TEXT("nodeName"));
	int32 NodeIndex = Json->GetIntegerField(TEXT("nodeIndex"));

	if (NodeName.IsEmpty() && NodeIndex < 0)
		return MakeErrorJson(TEXT("Must provide either nodeName or nodeIndex"));

	// Find the material
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> MatAssets;
	AssetRegistry.GetAssetsByClass(UMaterial::StaticClass()->GetClassPathName(), MatAssets, true);

	UMaterial* Material = nullptr;
	for (const FAssetData& Asset : MatAssets)
	{
		if (Asset.AssetName.ToString() == MaterialName || Asset.GetObjectPathString().Contains(MaterialName))
		{
			Material = Cast<UMaterial>(Asset.GetAsset());
			break;
		}
	}

	if (!Material)
		return MakeErrorJson(FString::Printf(TEXT("Material not found: %s"), *MaterialName));

	// Find the expression to remove
	UMaterialExpression* TargetExpr = nullptr;
	int32 FoundIndex = -1;

	for (int32 i = 0; i < Material->GetExpressions().Num(); i++)
	{
		UMaterialExpression* Expr = Material->GetExpressions()[i];
		if (!Expr) continue;

		if (!NodeName.IsEmpty() && Expr->GetName() == NodeName)
		{
			TargetExpr = Expr;
			FoundIndex = i;
			break;
		}
		else if (i == NodeIndex)
		{
			TargetExpr = Expr;
			FoundIndex = i;
			break;
		}
	}

	if (!TargetExpr)
		return MakeErrorJson(TEXT("Material expression node not found"));

	FString RemovedName = TargetExpr->GetName();
	FString RemovedClass = TargetExpr->GetClass()->GetName();

	// Remove the expression
	Material->GetExpressionCollection().RemoveExpression(TargetExpr);
	Material->PostEditChange();
	Material->MarkPackageDirty();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("materialName"), Material->GetName());
	Result->SetStringField(TEXT("removedNode"), RemovedName);
	Result->SetStringField(TEXT("removedClass"), RemovedClass);
	Result->SetNumberField(TEXT("removedIndex"), FoundIndex);
	Result->SetNumberField(TEXT("remainingExpressions"), Material->GetExpressions().Num());
	return JsonToString(Result);
}

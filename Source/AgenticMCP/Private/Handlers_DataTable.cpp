// Handlers_DataTable.cpp
// DataTable read/write handlers for AgenticMCP
// Allows reading and modifying UDataTable assets

// UE 5.6: Suppress C4459 warning (declaration hides global) from InterchangeCore
#pragma warning(push)
#pragma warning(disable: 4459)
#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/DataTable.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UObjectGlobals.h"

FString FAgenticMCPServer::HandleDataTableRead(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> BodyJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body. Expected: {\"path\": \"/Game/Path/To/DataTable\"}"));
	}

	FString TablePath = BodyJson->GetStringField(TEXT("path"));
	if (TablePath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'path' field"));
	}

	// Load the DataTable
	UDataTable* DataTable = LoadObject<UDataTable>(nullptr, *TablePath);
	if (!DataTable)
	{
		// Try with .DT_ prefix variations
		if (!TablePath.Contains(TEXT(".")))
		{
			TablePath = TablePath + TEXT(".") + FPaths::GetBaseFilename(TablePath);
			DataTable = LoadObject<UDataTable>(nullptr, *TablePath);
		}
	}

	if (!DataTable)
	{
		return MakeErrorJson(FString::Printf(TEXT("DataTable not found: %s"), *TablePath));
	}

	OutJson->SetStringField(TEXT("name"), DataTable->GetName());
	OutJson->SetStringField(TEXT("path"), DataTable->GetPathName());
	OutJson->SetStringField(TEXT("rowStructName"), DataTable->GetRowStructPathName().ToString());

	// Get all row names
	TArray<FName> RowNames = DataTable->GetRowNames();
	OutJson->SetNumberField(TEXT("rowCount"), RowNames.Num());

	// Export rows as JSON
	TArray<TSharedPtr<FJsonValue>> RowsArray;

	for (const FName& RowName : RowNames)
	{
		TSharedRef<FJsonObject> RowObj = MakeShared<FJsonObject>();
		RowObj->SetStringField(TEXT("rowName"), RowName.ToString());

		// Get the row data
		uint8* RowData = DataTable->FindRowUnchecked(RowName);
		if (RowData && DataTable->GetRowStruct())
		{
			// Iterate through properties of the row struct
			for (TFieldIterator<FProperty> PropIt(DataTable->GetRowStruct()); PropIt; ++PropIt)
			{
				FProperty* Property = *PropIt;
				FString PropName = Property->GetName();

				void* ValuePtr = Property->ContainerPtrToValuePtr<void>(RowData);

				// Handle different property types
				if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
				{
					RowObj->SetStringField(PropName, StrProp->GetPropertyValue(ValuePtr));
				}
				else if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
				{
					RowObj->SetStringField(PropName, NameProp->GetPropertyValue(ValuePtr).ToString());
				}
				else if (FIntProperty* IntProp = CastField<FIntProperty>(Property))
				{
					RowObj->SetNumberField(PropName, IntProp->GetPropertyValue(ValuePtr));
				}
				else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
				{
					RowObj->SetNumberField(PropName, FloatProp->GetPropertyValue(ValuePtr));
				}
				else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
				{
					RowObj->SetNumberField(PropName, DoubleProp->GetPropertyValue(ValuePtr));
				}
				else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
				{
					RowObj->SetBoolField(PropName, BoolProp->GetPropertyValue(ValuePtr));
				}
				else if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Property))
				{
					FSoftObjectPtr SoftPtr = SoftObjProp->GetPropertyValue(ValuePtr);
					RowObj->SetStringField(PropName, SoftPtr.ToString());
				}
				else if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
				{
					UObject* Obj = ObjProp->GetObjectPropertyValue(ValuePtr);
					RowObj->SetStringField(PropName, Obj ? Obj->GetPathName() : TEXT("None"));
				}
				else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
				{
					FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
					int64 EnumValue = UnderlyingProp->GetSignedIntPropertyValue(ValuePtr);
					UEnum* Enum = EnumProp->GetEnum();
					if (Enum)
					{
						RowObj->SetStringField(PropName, Enum->GetNameStringByValue(EnumValue));
					}
					else
					{
						RowObj->SetNumberField(PropName, EnumValue);
					}
				}
				else
				{
					// Fallback: export as string
					FString ValueStr;
					Property->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, nullptr, PPF_None);
					RowObj->SetStringField(PropName, ValueStr);
				}
			}
		}

		RowsArray.Add(MakeShared<FJsonValueObject>(RowObj));
	}

	OutJson->SetArrayField(TEXT("rows"), RowsArray);

	return JsonToString(OutJson);
}

FString FAgenticMCPServer::HandleDataTableWrite(const TMap<FString, FString>& Params, const FString& Body)
{
	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> BodyJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, BodyJson) || !BodyJson.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body. Expected: {\"path\": \"...\", \"rowName\": \"...\", \"values\": {...}}"));
	}

	FString TablePath = BodyJson->GetStringField(TEXT("path"));
	FString RowName = BodyJson->GetStringField(TEXT("rowName"));

	if (TablePath.IsEmpty() || RowName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'path' or 'rowName' field"));
	}

	const TSharedPtr<FJsonObject>* ValuesObj = nullptr;
	if (!BodyJson->TryGetObjectField(TEXT("values"), ValuesObj) || !ValuesObj)
	{
		return MakeErrorJson(TEXT("Missing 'values' object"));
	}

	// Load the DataTable
	UDataTable* DataTable = LoadObject<UDataTable>(nullptr, *TablePath);
	if (!DataTable)
	{
		if (!TablePath.Contains(TEXT(".")))
		{
			TablePath = TablePath + TEXT(".") + FPaths::GetBaseFilename(TablePath);
			DataTable = LoadObject<UDataTable>(nullptr, *TablePath);
		}
	}

	if (!DataTable)
	{
		return MakeErrorJson(FString::Printf(TEXT("DataTable not found: %s"), *TablePath));
	}

	// Find the row
	uint8* RowData = DataTable->FindRowUnchecked(FName(*RowName));
	if (!RowData)
	{
		return MakeErrorJson(FString::Printf(TEXT("Row not found: %s"), *RowName));
	}

	if (!DataTable->GetRowStruct())
	{
		return MakeErrorJson(TEXT("DataTable has no row struct"));
	}

	// Mark the package dirty for saving
	DataTable->Modify();

	int32 PropertiesModified = 0;
	TArray<FString> ModifiedProps;

	// Iterate through values to set
	for (const auto& Pair : (*ValuesObj)->Values)
	{
		FString PropName = Pair.Key;

		FProperty* Property = DataTable->GetRowStruct()->FindPropertyByName(FName(*PropName));
		if (!Property)
		{
			continue;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(RowData);

		// Handle different property types
		if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
		{
			if (Pair.Value->Type == EJson::String)
			{
				StrProp->SetPropertyValue(ValuePtr, Pair.Value->AsString());
				PropertiesModified++;
				ModifiedProps.Add(PropName);
			}
		}
		else if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
		{
			if (Pair.Value->Type == EJson::String)
			{
				NameProp->SetPropertyValue(ValuePtr, FName(*Pair.Value->AsString()));
				PropertiesModified++;
				ModifiedProps.Add(PropName);
			}
		}
		else if (FIntProperty* IntProp = CastField<FIntProperty>(Property))
		{
			if (Pair.Value->Type == EJson::Number)
			{
				IntProp->SetPropertyValue(ValuePtr, static_cast<int32>(Pair.Value->AsNumber()));
				PropertiesModified++;
				ModifiedProps.Add(PropName);
			}
		}
		else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
		{
			if (Pair.Value->Type == EJson::Number)
			{
				FloatProp->SetPropertyValue(ValuePtr, static_cast<float>(Pair.Value->AsNumber()));
				PropertiesModified++;
				ModifiedProps.Add(PropName);
			}
		}
		else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
		{
			if (Pair.Value->Type == EJson::Boolean)
			{
				BoolProp->SetPropertyValue(ValuePtr, Pair.Value->AsBool());
				PropertiesModified++;
				ModifiedProps.Add(PropName);
			}
		}
		else if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Property))
		{
			if (Pair.Value->Type == EJson::String)
			{
				FSoftObjectPath SoftPath(Pair.Value->AsString());
				FSoftObjectPtr SoftPtr(SoftPath);
				SoftObjProp->SetPropertyValue(ValuePtr, SoftPtr);
				PropertiesModified++;
				ModifiedProps.Add(PropName);
			}
		}
	}

	OutJson->SetBoolField(TEXT("success"), PropertiesModified > 0);
	OutJson->SetStringField(TEXT("dataTable"), DataTable->GetName());
	OutJson->SetStringField(TEXT("rowName"), RowName);
	OutJson->SetNumberField(TEXT("propertiesModified"), PropertiesModified);

	TArray<TSharedPtr<FJsonValue>> ModifiedArray;
	for (const FString& Prop : ModifiedProps)
	{
		ModifiedArray.Add(MakeShared<FJsonValueString>(Prop));
	}
	OutJson->SetArrayField(TEXT("modifiedProperties"), ModifiedArray);

	return JsonToString(OutJson);
}

// ============================================================================
// DATATABLE MUTATION HANDLERS
// ============================================================================

// --- dataTableAddRow ---
// Add a new row to a DataTable.
// Body: { "path": "/Game/Data/DT_Items", "rowName": "NewItem", "values": { "Name": "Sword", "Damage": 50 } }
FString FAgenticMCPServer::HandleDataTableAddRow(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString TablePath = Json->GetStringField(TEXT("path"));
	FString RowName = Json->GetStringField(TEXT("rowName"));
	if (TablePath.IsEmpty() || RowName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'path' or 'rowName'"));
	}

	UDataTable* DataTable = LoadObject<UDataTable>(nullptr, *TablePath);
	if (!DataTable)
	{
		TablePath = TablePath + TEXT(".") + FPaths::GetBaseFilename(TablePath);
		DataTable = LoadObject<UDataTable>(nullptr, *TablePath);
	}
	if (!DataTable)
	{
		return MakeErrorJson(FString::Printf(TEXT("DataTable not found: %s"), *TablePath));
	}

	if (!DataTable->GetRowStruct())
	{
		return MakeErrorJson(TEXT("DataTable has no row struct"));
	}

	// Check if row already exists
	if (DataTable->FindRowUnchecked(FName(*RowName)))
	{
		return MakeErrorJson(FString::Printf(TEXT("Row already exists: %s"), *RowName));
	}

	// Add empty row
	DataTable->Modify();
	FTableRowBase* NewRow = (FTableRowBase*)FMemory::Malloc(DataTable->GetRowStruct()->GetStructureSize());
	DataTable->GetRowStruct()->InitializeStruct(NewRow);
	DataTable->AddRow(FName(*RowName), *NewRow);

	// Now set values if provided
	const TSharedPtr<FJsonObject>* ValuesObj = nullptr;
	if (Json->TryGetObjectField(TEXT("values"), ValuesObj) && ValuesObj)
	{
		uint8* RowData = DataTable->FindRowUnchecked(FName(*RowName));
		if (RowData)
		{
			for (TFieldIterator<FProperty> PropIt(DataTable->GetRowStruct()); PropIt; ++PropIt)
			{
				FProperty* Prop = *PropIt;
				FString PropName = Prop->GetName();
				if ((*ValuesObj)->HasField(PropName))
				{
					FString ValueStr = (*ValuesObj)->Values.Find(PropName)->Get()->AsString();
					void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(RowData);
					Prop->ImportText_Direct(*ValueStr, ValuePtr, nullptr, PPF_None);
				}
			}
		}
	}

	FMemory::Free(NewRow);
	DataTable->MarkPackageDirty();

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	OutJson->SetStringField(TEXT("rowName"), RowName);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, Writer);
	return Out;
}

// --- dataTableDeleteRow ---
// Delete a row from a DataTable.
// Body: { "path": "/Game/Data/DT_Items", "rowName": "OldItem" }
FString FAgenticMCPServer::HandleDataTableDeleteRow(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString TablePath = Json->GetStringField(TEXT("path"));
	FString RowName = Json->GetStringField(TEXT("rowName"));
	if (TablePath.IsEmpty() || RowName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'path' or 'rowName'"));
	}

	UDataTable* DataTable = LoadObject<UDataTable>(nullptr, *TablePath);
	if (!DataTable)
	{
		TablePath = TablePath + TEXT(".") + FPaths::GetBaseFilename(TablePath);
		DataTable = LoadObject<UDataTable>(nullptr, *TablePath);
	}
	if (!DataTable)
	{
		return MakeErrorJson(FString::Printf(TEXT("DataTable not found: %s"), *TablePath));
	}

	if (!DataTable->FindRowUnchecked(FName(*RowName)))
	{
		return MakeErrorJson(FString::Printf(TEXT("Row not found: %s"), *RowName));
	}

	DataTable->Modify();
	DataTable->RemoveRow(FName(*RowName));
	DataTable->MarkPackageDirty();

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("status"), TEXT("ok"));
	OutJson->SetStringField(TEXT("deletedRow"), RowName);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, Writer);
	return Out;
}

// --- dataTableGetSchema ---
// Get the row struct schema (field names, types) for a DataTable.
// Body: { "path": "/Game/Data/DT_Items" }
FString FAgenticMCPServer::HandleDataTableGetSchema(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body"));
	}

	FString TablePath = Json->GetStringField(TEXT("path"));
	if (TablePath.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing 'path'"));
	}

	UDataTable* DataTable = LoadObject<UDataTable>(nullptr, *TablePath);
	if (!DataTable)
	{
		TablePath = TablePath + TEXT(".") + FPaths::GetBaseFilename(TablePath);
		DataTable = LoadObject<UDataTable>(nullptr, *TablePath);
	}
	if (!DataTable)
	{
		return MakeErrorJson(FString::Printf(TEXT("DataTable not found: %s"), *TablePath));
	}

	if (!DataTable->GetRowStruct())
	{
		return MakeErrorJson(TEXT("DataTable has no row struct"));
	}

	TArray<TSharedPtr<FJsonValue>> FieldsArr;
	for (TFieldIterator<FProperty> PropIt(DataTable->GetRowStruct()); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;
		TSharedRef<FJsonObject> FieldObj = MakeShared<FJsonObject>();
		FieldObj->SetStringField(TEXT("name"), Prop->GetName());
		FieldObj->SetStringField(TEXT("type"), Prop->GetCPPType());
		FieldObj->SetNumberField(TEXT("size"), Prop->GetSize());
		FieldObj->SetNumberField(TEXT("offset"), Prop->GetOffset_ForInternal());
		FieldsArr.Add(MakeShared<FJsonValueObject>(FieldObj));
	}

	TSharedRef<FJsonObject> OutJson = MakeShared<FJsonObject>();
	OutJson->SetStringField(TEXT("structName"), DataTable->GetRowStruct()->GetName());
	OutJson->SetNumberField(TEXT("rowCount"), DataTable->GetRowMap().Num());
	OutJson->SetArrayField(TEXT("fields"), FieldsArr);
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(OutJson, Writer);
	return Out;
}

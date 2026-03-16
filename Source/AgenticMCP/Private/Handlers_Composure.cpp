// Handlers_Composure.cpp
// Composure compositing system handlers for AgenticMCP.
// UE 5.6 target. Composure elements, layers, passes.
#include "AgenticMCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPComposure, Log, All);

// Helper: Find a UClass by short name across all loaded packages.
// Uses FindFirstObject on UE 5.1+ (ANY_PACKAGE was removed).
static UClass* FindClassByName(const TCHAR* ClassName)
{
	// Try FindFirstObject first (UE 5.1+)
	UClass* Found = FindFirstObject<UClass>(ClassName, EFindFirstObjectOptions::NativeFirst);
	if (Found) return Found;

	// Fallback: iterate loaded classes
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->GetName() == ClassName)
		{
			return *It;
		}
	}
	return nullptr;
}

// ============================================================
// composureList
// List all Composure elements in the scene
// ============================================================
FString FAgenticMCPServer::HandleComposureList(const FString& Body)
{
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return MakeErrorJson(TEXT("No editor world"));
    }

    // Find CompositingElement actors
    UClass* CompElementClass = FindClassByName(TEXT("CompositingElement"));

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteValue(TEXT("composureAvailable"), CompElementClass != nullptr);

    Writer->WriteArrayStart(TEXT("elements"));
    if (CompElementClass)
    {
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if ((*It)->IsA(CompElementClass))
            {
                Writer->WriteObjectStart();
                Writer->WriteValue(TEXT("name"), (*It)->GetActorLabel());
                Writer->WriteValue(TEXT("class"), (*It)->GetClass()->GetName());
                Writer->WriteObjectEnd();
            }
        }
    }
    Writer->WriteArrayEnd();

    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// composureCreateElement
// Create a Composure compositing element
// Params: name (string), elementType (string: CG/MediaPlate/Empty, default Empty)
//         location (object: x,y,z, optional)
// ============================================================
FString FAgenticMCPServer::HandleComposureCreateElement(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString Name = Json->GetStringField(TEXT("name"));
    FString ElementType = Json->HasField(TEXT("elementType")) ? Json->GetStringField(TEXT("elementType")) : TEXT("Empty");

    if (Name.IsEmpty())
    {
        return MakeErrorJson(TEXT("name is required"));
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return MakeErrorJson(TEXT("No editor world"));
    }

    // Map element type to class
    FString ClassName;
    if (ElementType == TEXT("CG")) ClassName = TEXT("CompositingCaptureBase");
    else if (ElementType == TEXT("MediaPlate")) ClassName = TEXT("CompositingMediaInput");
    else ClassName = TEXT("CompositingElement");

    UClass* ElementClass = FindClassByName(*ClassName);
    if (!ElementClass)
    {
        // Fallback to base CompositingElement
        ElementClass = FindClassByName(TEXT("CompositingElement"));
    }

    if (!ElementClass)
    {
        return MakeErrorJson(TEXT("Composure plugin is not loaded. Enable the Composure plugin."));
    }

    FVector Location(0, 0, 0);
    if (Json->HasField(TEXT("location")))
    {
        auto Loc = Json->GetObjectField(TEXT("location"));
        Location.X = Loc->GetNumberField(TEXT("x"));
        Location.Y = Loc->GetNumberField(TEXT("y"));
        Location.Z = Loc->GetNumberField(TEXT("z"));
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    FTransform SpawnTransform(FRotator::ZeroRotator, Location);
    AActor* NewActor = World->SpawnActor<AActor>(ElementClass, SpawnTransform, Params);
    if (!NewActor)
    {
        return MakeErrorJson(TEXT("Failed to spawn Composure element"));
    }

    NewActor->SetActorLabel(*Name);

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("success"), true);
    Writer->WriteValue(TEXT("name"), Name);
    Writer->WriteValue(TEXT("elementType"), ElementType);
    Writer->WriteValue(TEXT("class"), NewActor->GetClass()->GetName());
    Writer->WriteObjectEnd();
    Writer->Close();
    return Result;
}

// ============================================================
// composureAddPass
// Add a compositing pass to an element
// Params: elementName (string), passType (string), passName (string)
// ============================================================
FString FAgenticMCPServer::HandleComposureAddPass(const FString& Body)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
    {
        return MakeErrorJson(TEXT("Invalid JSON body"));
    }

    FString ElementName = Json->GetStringField(TEXT("elementName"));
    FString PassType = Json->GetStringField(TEXT("passType"));
    FString PassName = Json->HasField(TEXT("passName")) ? Json->GetStringField(TEXT("passName")) : PassType;

    if (ElementName.IsEmpty() || PassType.IsEmpty())
    {
        return MakeErrorJson(TEXT("elementName and passType are required"));
    }

    FString PythonCmd = FString::Printf(
        TEXT("import unreal; "
             "actors = unreal.EditorLevelLibrary.get_all_level_actors(); "
             "elem = next((a for a in actors if a.get_actor_label() == '%s'), None); "
             "if elem: "
             "  pass_class = unreal.find_class('%s'); "
             "  if pass_class: "
             "    elem.create_new_pass(pass_class, '%s'); "
             "    print('SUCCESS') "
             "  else: print('PASS_CLASS_NOT_FOUND') "
             "else: print('ELEMENT_NOT_FOUND')"),
        *ElementName, *PassType, *PassName);

    if (FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {
        FString ExecCmd = FString::Printf(TEXT("py %s"), *PythonCmd);
        GEditor->Exec(GEditor->GetEditorWorldContext().World(), *ExecCmd);
        {
            FString Result;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
            Writer->WriteObjectStart();
            Writer->WriteValue(TEXT("success"), true);
            Writer->WriteValue(TEXT("elementName"), ElementName);
            Writer->WriteValue(TEXT("passType"), PassType);
            Writer->WriteValue(TEXT("passName"), PassName);
            Writer->WriteObjectEnd();
            Writer->Close();
            return Result;
        }
    }

    return MakeErrorJson(TEXT("PythonScriptPlugin not loaded."));
}

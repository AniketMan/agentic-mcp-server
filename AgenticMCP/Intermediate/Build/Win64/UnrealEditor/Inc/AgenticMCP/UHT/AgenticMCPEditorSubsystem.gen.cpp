// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "AgenticMCPEditorSubsystem.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

void EmptyLinkFunctionForGeneratedCodeAgenticMCPEditorSubsystem() {}

// ********** Begin Cross Module References ********************************************************
AGENTICMCP_API UClass* Z_Construct_UClass_UAgenticMCPEditorSubsystem();
AGENTICMCP_API UClass* Z_Construct_UClass_UAgenticMCPEditorSubsystem_NoRegister();
EDITORSUBSYSTEM_API UClass* Z_Construct_UClass_UEditorSubsystem();
UPackage* Z_Construct_UPackage__Script_AgenticMCP();
// ********** End Cross Module References **********************************************************

// ********** Begin Class UAgenticMCPEditorSubsystem ***********************************************
void UAgenticMCPEditorSubsystem::StaticRegisterNativesUAgenticMCPEditorSubsystem()
{
}
FClassRegistrationInfo Z_Registration_Info_UClass_UAgenticMCPEditorSubsystem;
UClass* UAgenticMCPEditorSubsystem::GetPrivateStaticClass()
{
	using TClass = UAgenticMCPEditorSubsystem;
	if (!Z_Registration_Info_UClass_UAgenticMCPEditorSubsystem.InnerSingleton)
	{
		GetPrivateStaticClassBody(
			StaticPackage(),
			TEXT("AgenticMCPEditorSubsystem"),
			Z_Registration_Info_UClass_UAgenticMCPEditorSubsystem.InnerSingleton,
			StaticRegisterNativesUAgenticMCPEditorSubsystem,
			sizeof(TClass),
			alignof(TClass),
			TClass::StaticClassFlags,
			TClass::StaticClassCastFlags(),
			TClass::StaticConfigName(),
			(UClass::ClassConstructorType)InternalConstructor<TClass>,
			(UClass::ClassVTableHelperCtorCallerType)InternalVTableHelperCtorCaller<TClass>,
			UOBJECT_CPPCLASS_STATICFUNCTIONS_FORCLASS(TClass),
			&TClass::Super::StaticClass,
			&TClass::WithinClass::StaticClass
		);
	}
	return Z_Registration_Info_UClass_UAgenticMCPEditorSubsystem.InnerSingleton;
}
UClass* Z_Construct_UClass_UAgenticMCPEditorSubsystem_NoRegister()
{
	return UAgenticMCPEditorSubsystem::GetPrivateStaticClass();
}
struct Z_Construct_UClass_UAgenticMCPEditorSubsystem_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
		{ "Comment", "/**\n * UAgenticMCPEditorSubsystem\n *\n * Automatically starts the AgenticMCP HTTP server when the UE5 editor opens.\n * The MCP TypeScript wrapper connects to this server over localhost:9847.\n *\n * This is the preferred serving mode \xe2\x80\x94 zero extra RAM, instant startup,\n * full access to all editor APIs including level management.\n */" },
		{ "IncludePath", "AgenticMCPEditorSubsystem.h" },
		{ "ModuleRelativePath", "Public/AgenticMCPEditorSubsystem.h" },
		{ "ToolTip", "UAgenticMCPEditorSubsystem\n\nAutomatically starts the AgenticMCP HTTP server when the UE5 editor opens.\nThe MCP TypeScript wrapper connects to this server over localhost:9847.\n\nThis is the preferred serving mode \xe2\x80\x94 zero extra RAM, instant startup,\nfull access to all editor APIs including level management." },
	};
#endif // WITH_METADATA
	static UObject* (*const DependentSingletons[])();
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UAgenticMCPEditorSubsystem>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
};
UObject* (*const Z_Construct_UClass_UAgenticMCPEditorSubsystem_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_UEditorSubsystem,
	(UObject* (*)())Z_Construct_UPackage__Script_AgenticMCP,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UAgenticMCPEditorSubsystem_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_UAgenticMCPEditorSubsystem_Statics::ClassParams = {
	&UAgenticMCPEditorSubsystem::StaticClass,
	nullptr,
	&StaticCppClassTypeInfo,
	DependentSingletons,
	nullptr,
	nullptr,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	0,
	0,
	0,
	0x000000A0u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_UAgenticMCPEditorSubsystem_Statics::Class_MetaDataParams), Z_Construct_UClass_UAgenticMCPEditorSubsystem_Statics::Class_MetaDataParams)
};
UClass* Z_Construct_UClass_UAgenticMCPEditorSubsystem()
{
	if (!Z_Registration_Info_UClass_UAgenticMCPEditorSubsystem.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_UAgenticMCPEditorSubsystem.OuterSingleton, Z_Construct_UClass_UAgenticMCPEditorSubsystem_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_UAgenticMCPEditorSubsystem.OuterSingleton;
}
UAgenticMCPEditorSubsystem::UAgenticMCPEditorSubsystem() {}
DEFINE_VTABLE_PTR_HELPER_CTOR(UAgenticMCPEditorSubsystem);
UAgenticMCPEditorSubsystem::~UAgenticMCPEditorSubsystem() {}
// ********** End Class UAgenticMCPEditorSubsystem *************************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID_Engine_Plugins_AgenticMCP_Source_AgenticMCP_Public_AgenticMCPEditorSubsystem_h__Script_AgenticMCP_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_UAgenticMCPEditorSubsystem, UAgenticMCPEditorSubsystem::StaticClass, TEXT("UAgenticMCPEditorSubsystem"), &Z_Registration_Info_UClass_UAgenticMCPEditorSubsystem, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(UAgenticMCPEditorSubsystem), 4046977490U) },
	};
};
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_Engine_Plugins_AgenticMCP_Source_AgenticMCP_Public_AgenticMCPEditorSubsystem_h__Script_AgenticMCP_29746957(TEXT("/Script/AgenticMCP"),
	Z_CompiledInDeferFile_FID_Engine_Plugins_AgenticMCP_Source_AgenticMCP_Public_AgenticMCPEditorSubsystem_h__Script_AgenticMCP_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_Engine_Plugins_AgenticMCP_Source_AgenticMCP_Public_AgenticMCPEditorSubsystem_h__Script_AgenticMCP_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0);
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS

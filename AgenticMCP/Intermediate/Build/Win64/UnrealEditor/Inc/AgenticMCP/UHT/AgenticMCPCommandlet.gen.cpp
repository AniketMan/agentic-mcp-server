// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "AgenticMCPCommandlet.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

void EmptyLinkFunctionForGeneratedCodeAgenticMCPCommandlet() {}

// ********** Begin Cross Module References ********************************************************
AGENTICMCP_API UClass* Z_Construct_UClass_UAgenticMCPCommandlet();
AGENTICMCP_API UClass* Z_Construct_UClass_UAgenticMCPCommandlet_NoRegister();
ENGINE_API UClass* Z_Construct_UClass_UCommandlet();
UPackage* Z_Construct_UPackage__Script_AgenticMCP();
// ********** End Cross Module References **********************************************************

// ********** Begin Class UAgenticMCPCommandlet ****************************************************
void UAgenticMCPCommandlet::StaticRegisterNativesUAgenticMCPCommandlet()
{
}
FClassRegistrationInfo Z_Registration_Info_UClass_UAgenticMCPCommandlet;
UClass* UAgenticMCPCommandlet::GetPrivateStaticClass()
{
	using TClass = UAgenticMCPCommandlet;
	if (!Z_Registration_Info_UClass_UAgenticMCPCommandlet.InnerSingleton)
	{
		GetPrivateStaticClassBody(
			StaticPackage(),
			TEXT("AgenticMCPCommandlet"),
			Z_Registration_Info_UClass_UAgenticMCPCommandlet.InnerSingleton,
			StaticRegisterNativesUAgenticMCPCommandlet,
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
	return Z_Registration_Info_UClass_UAgenticMCPCommandlet.InnerSingleton;
}
UClass* Z_Construct_UClass_UAgenticMCPCommandlet_NoRegister()
{
	return UAgenticMCPCommandlet::GetPrivateStaticClass();
}
struct Z_Construct_UClass_UAgenticMCPCommandlet_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
		{ "Comment", "/**\n * UAgenticMCPCommandlet\n *\n * Runs the AgenticMCP HTTP server in a headless UnrealEditor-Cmd process.\n * Useful for CI/CD pipelines or when the editor is not open.\n *\n * The commandlet manually ticks the engine to process HTTP requests.\n * It runs until /api/shutdown is called or the process is killed.\n */" },
		{ "IncludePath", "AgenticMCPCommandlet.h" },
		{ "ModuleRelativePath", "Public/AgenticMCPCommandlet.h" },
		{ "ToolTip", "UAgenticMCPCommandlet\n\nRuns the AgenticMCP HTTP server in a headless UnrealEditor-Cmd process.\nUseful for CI/CD pipelines or when the editor is not open.\n\nThe commandlet manually ticks the engine to process HTTP requests.\nIt runs until /api/shutdown is called or the process is killed." },
	};
#endif // WITH_METADATA
	static UObject* (*const DependentSingletons[])();
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UAgenticMCPCommandlet>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
};
UObject* (*const Z_Construct_UClass_UAgenticMCPCommandlet_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_UCommandlet,
	(UObject* (*)())Z_Construct_UPackage__Script_AgenticMCP,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UAgenticMCPCommandlet_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_UAgenticMCPCommandlet_Statics::ClassParams = {
	&UAgenticMCPCommandlet::StaticClass,
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
	0x000000A8u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_UAgenticMCPCommandlet_Statics::Class_MetaDataParams), Z_Construct_UClass_UAgenticMCPCommandlet_Statics::Class_MetaDataParams)
};
UClass* Z_Construct_UClass_UAgenticMCPCommandlet()
{
	if (!Z_Registration_Info_UClass_UAgenticMCPCommandlet.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_UAgenticMCPCommandlet.OuterSingleton, Z_Construct_UClass_UAgenticMCPCommandlet_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_UAgenticMCPCommandlet.OuterSingleton;
}
DEFINE_VTABLE_PTR_HELPER_CTOR(UAgenticMCPCommandlet);
UAgenticMCPCommandlet::~UAgenticMCPCommandlet() {}
// ********** End Class UAgenticMCPCommandlet ******************************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID_Engine_Plugins_AgenticMCP_Source_AgenticMCP_Public_AgenticMCPCommandlet_h__Script_AgenticMCP_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_UAgenticMCPCommandlet, UAgenticMCPCommandlet::StaticClass, TEXT("UAgenticMCPCommandlet"), &Z_Registration_Info_UClass_UAgenticMCPCommandlet, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(UAgenticMCPCommandlet), 1460079547U) },
	};
};
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_Engine_Plugins_AgenticMCP_Source_AgenticMCP_Public_AgenticMCPCommandlet_h__Script_AgenticMCP_1582644890(TEXT("/Script/AgenticMCP"),
	Z_CompiledInDeferFile_FID_Engine_Plugins_AgenticMCP_Source_AgenticMCP_Public_AgenticMCPCommandlet_h__Script_AgenticMCP_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_Engine_Plugins_AgenticMCP_Source_AgenticMCP_Public_AgenticMCPCommandlet_h__Script_AgenticMCP_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0);
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS

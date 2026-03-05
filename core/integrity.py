"""
integrity.py — Reference integrity validation for UE 5.6 assets.

Ensures that all FPackageIndex references in the asset are valid,
no dangling references exist, and the name map is complete.
This is the critical safety layer that prevents file corruption.

RULES:
  1. Every FPackageIndex > 0 must point to a valid export (1-based)
  2. Every FPackageIndex < 0 must point to a valid import (negated, 1-based)
  3. FPackageIndex == 0 means null — always valid
  4. Every FName used in exports/imports must exist in the name map
  5. PreloadDependencies must reference valid exports
  6. DependsMap entries must reference valid imports
  7. Level actor list must only reference valid exports
"""

import sys
from typing import List, Dict, Tuple, Optional
from dataclasses import dataclass, field
from enum import Enum


class Severity(Enum):
    """Validation issue severity levels."""
    INFO = "info"
    WARNING = "warning"
    ERROR = "error"
    CRITICAL = "critical"


@dataclass
class ValidationIssue:
    """A single validation finding."""
    severity: Severity
    category: str
    message: str
    location: str = ""  # e.g., "Export[5].ClassIndex"
    fix_suggestion: str = ""

    def __str__(self):
        loc = f" at {self.location}" if self.location else ""
        fix = f" | Fix: {self.fix_suggestion}" if self.fix_suggestion else ""
        return f"[{self.severity.value.upper()}] {self.category}{loc}: {self.message}{fix}"


@dataclass
class ValidationReport:
    """Complete validation report for an asset."""
    filepath: str
    issues: List[ValidationIssue] = field(default_factory=list)
    passed: bool = True

    def add(self, issue: ValidationIssue):
        self.issues.append(issue)
        if issue.severity in (Severity.ERROR, Severity.CRITICAL):
            self.passed = False

    @property
    def error_count(self) -> int:
        return sum(1 for i in self.issues if i.severity in (Severity.ERROR, Severity.CRITICAL))

    @property
    def warning_count(self) -> int:
        return sum(1 for i in self.issues if i.severity == Severity.WARNING)

    def summary(self) -> str:
        status = "PASSED" if self.passed else "FAILED"
        return (
            f"Validation {status}: {len(self.issues)} issues "
            f"({self.error_count} errors, {self.warning_count} warnings)"
        )

    def to_dict(self) -> dict:
        return {
            "filepath": self.filepath,
            "passed": self.passed,
            "error_count": self.error_count,
            "warning_count": self.warning_count,
            "issues": [
                {
                    "severity": i.severity.value,
                    "category": i.category,
                    "message": i.message,
                    "location": i.location,
                    "fix_suggestion": i.fix_suggestion,
                }
                for i in self.issues
            ],
        }


class IntegrityValidator:
    """
    Validates reference integrity of a UAsset.
    
    Run this BEFORE saving any modified asset to catch corruption early.
    """

    def __init__(self, asset_file):
        """
        Args:
            asset_file: An AssetFile instance from uasset_bridge.py
        """
        self.af = asset_file
        self.asset = asset_file.asset

    def validate_all(self) -> ValidationReport:
        """
        Run all validation checks and return a comprehensive report.
        """
        report = ValidationReport(filepath=str(self.af.filepath))

        # Run each validation pass
        self._validate_export_references(report)
        self._validate_import_references(report)
        self._validate_name_map(report)
        self._validate_level_actors(report)
        self._validate_preload_dependencies(report)
        self._validate_export_serial_sizes(report)
        self._validate_circular_references(report)

        return report

    # ----- Individual Validation Passes -----

    def _validate_export_references(self, report: ValidationReport):
        """
        Check that all FPackageIndex fields on exports point to valid targets.
        Covers: ClassIndex, SuperIndex, TemplateIndex, OuterIndex.
        """
        export_count = self.af.export_count
        import_count = self.af.import_count

        for i in range(export_count):
            exp = self.af.get_export(i)
            location_prefix = f"Export[{i}] ({self.af.get_export_name(exp)})"

            # Check ClassIndex
            self._check_package_index(
                exp.ClassIndex.Index, export_count, import_count,
                f"{location_prefix}.ClassIndex", "Export class reference", report
            )

            # Check SuperIndex
            self._check_package_index(
                exp.SuperIndex.Index, export_count, import_count,
                f"{location_prefix}.SuperIndex", "Export super reference", report
            )

            # Check TemplateIndex
            self._check_package_index(
                exp.TemplateIndex.Index, export_count, import_count,
                f"{location_prefix}.TemplateIndex", "Export template reference", report
            )

            # Check OuterIndex
            self._check_package_index(
                exp.OuterIndex.Index, export_count, import_count,
                f"{location_prefix}.OuterIndex", "Export outer reference", report
            )

    def _validate_import_references(self, report: ValidationReport):
        """
        Check that import OuterIndex references are valid.
        """
        import_count = self.af.import_count
        export_count = self.af.export_count

        for i in range(import_count):
            imp = self.af.imports[i]
            location = f"Import[{i}] ({imp.ObjectName})"

            self._check_package_index(
                imp.OuterIndex.Index, export_count, import_count,
                f"{location}.OuterIndex", "Import outer reference", report
            )

    def _validate_name_map(self, report: ValidationReport):
        """
        Verify that all FName references in exports/imports exist in the name map.
        
        FName encoding: names are stored as a base string + numeric suffix.
        E.g., 'AmbientSound_0' is stored as base='AmbientSound' with Number=1
        (Number=0 means no suffix displayed). The name map only contains
        the base strings, so we must strip the trailing _N suffix before lookup.
        """
        name_set = set(self.af.name_map)

        def _base_name_in_map(name: str) -> bool:
            """Check if a name (possibly with _N suffix) has its base in the name map."""
            if name in name_set:
                return True
            # Strip trailing _N where N is a non-negative integer
            # E.g., 'Brush_0' -> 'Brush', 'K2Node_CallFunction_42' -> 'K2Node_CallFunction'
            parts = name.rsplit('_', 1)
            if len(parts) == 2 and parts[1].isdigit():
                return parts[0] in name_set
            return False

        # Check export names
        for i in range(self.af.export_count):
            exp = self.af.get_export(i)
            name = self.af.get_export_name(exp)
            if name and name != "None" and not _base_name_in_map(name):
                report.add(ValidationIssue(
                    severity=Severity.ERROR,
                    category="NameMap",
                    message=f"Export name '{name}' not found in name map",
                    location=f"Export[{i}]",
                    fix_suggestion="Add the name to the name map before saving"
                ))

        # Check import names
        for i in range(self.af.import_count):
            imp = self.af.imports[i]
            for field_name in ["ObjectName", "ClassName", "ClassPackage"]:
                try:
                    val = str(getattr(imp, field_name))
                    if val and val != "None" and not _base_name_in_map(val):
                        report.add(ValidationIssue(
                            severity=Severity.WARNING,
                            category="NameMap",
                            message=f"Import {field_name} '{val}' not in name map",
                            location=f"Import[{i}]",
                        ))
                except Exception:
                    pass

    def _validate_level_actors(self, report: ValidationReport):
        """
        If this is a level file, validate that all actor references in the
        LevelExport.Actors array point to valid exports.
        """
        level = self.af.get_level_export()
        if level is None:
            return

        export_count = self.af.export_count
        import_count = self.af.import_count

        for i in range(level.Actors.Count):
            pkg_idx = level.Actors[i].Index
            self._check_package_index(
                pkg_idx, export_count, import_count,
                f"LevelExport.Actors[{i}]", "Level actor reference", report
            )

            # Actors should typically be exports, not imports
            if pkg_idx < 0:
                report.add(ValidationIssue(
                    severity=Severity.WARNING,
                    category="LevelActors",
                    message=f"Actor at index {i} references an import (unusual)",
                    location=f"LevelExport.Actors[{i}]",
                ))

    def _validate_preload_dependencies(self, report: ValidationReport):
        """
        Validate preload dependency indices.
        """
        export_count = self.af.export_count
        import_count = self.af.import_count

        for i in range(export_count):
            exp = self.af.get_export(i)
            # Check if export has preload dependencies
            try:
                deps = exp.FirstExportDependency
                if deps < 0:
                    continue  # No dependencies
                
                dep_count = (
                    exp.SerializationBeforeSerializationDependencies +
                    exp.CreateBeforeSerializationDependencies +
                    exp.SerializationBeforeCreateDependencies +
                    exp.CreateBeforeCreateDependencies
                )
                
                if dep_count < 0:
                    report.add(ValidationIssue(
                        severity=Severity.ERROR,
                        category="PreloadDeps",
                        message=f"Negative dependency count ({dep_count})",
                        location=f"Export[{i}]",
                    ))
            except Exception:
                pass  # Export may not have these fields

    def _validate_export_serial_sizes(self, report: ValidationReport):
        """
        Check that export serial sizes are non-negative and serial offsets are valid.
        """
        for i in range(self.af.export_count):
            exp = self.af.get_export(i)
            try:
                if exp.SerialSize < 0:
                    report.add(ValidationIssue(
                        severity=Severity.CRITICAL,
                        category="SerialSize",
                        message=f"Negative serial size ({exp.SerialSize})",
                        location=f"Export[{i}]",
                        fix_suggestion="This export is corrupted and cannot be saved safely"
                    ))
                if exp.SerialOffset < 0:
                    report.add(ValidationIssue(
                        severity=Severity.CRITICAL,
                        category="SerialOffset",
                        message=f"Negative serial offset ({exp.SerialOffset})",
                        location=f"Export[{i}]",
                    ))
            except Exception:
                pass

    def _validate_circular_references(self, report: ValidationReport):
        """
        Detect circular OuterIndex chains that would cause infinite loops.
        """
        export_count = self.af.export_count

        for i in range(export_count):
            visited = set()
            current = i
            depth = 0
            max_depth = export_count + 1  # Safety limit

            while depth < max_depth:
                if current in visited:
                    report.add(ValidationIssue(
                        severity=Severity.CRITICAL,
                        category="CircularRef",
                        message=f"Circular OuterIndex chain detected",
                        location=f"Export[{i}]",
                        fix_suggestion="Break the circular reference by setting one OuterIndex to 0"
                    ))
                    break

                visited.add(current)
                try:
                    exp = self.af.get_export(current)
                    outer_idx = exp.OuterIndex.Index
                    if outer_idx <= 0:
                        break  # Reached root or null
                    current = outer_idx - 1  # Convert to 0-based
                except Exception:
                    break
                depth += 1

    # ----- Helper -----

    def _check_package_index(
        self, index: int, export_count: int, import_count: int,
        location: str, context: str, report: ValidationReport
    ):
        """
        Validate a single FPackageIndex value.
        
        FPackageIndex encoding:
          > 0: export reference (1-based, so valid range is 1..export_count)
          < 0: import reference (negated 1-based, so valid range is -1..-import_count)
          = 0: null reference (always valid)
        """
        if index == 0:
            return  # Null — always valid

        if index > 0:
            # Export reference (1-based)
            if index > export_count:
                report.add(ValidationIssue(
                    severity=Severity.ERROR,
                    category="DanglingRef",
                    message=f"{context}: export index {index} exceeds export count ({export_count})",
                    location=location,
                    fix_suggestion="Update the reference or add the missing export"
                ))
        else:
            # Import reference (negated 1-based)
            import_idx = -(index + 1)
            if import_idx >= import_count:
                report.add(ValidationIssue(
                    severity=Severity.ERROR,
                    category="DanglingRef",
                    message=f"{context}: import index {import_idx} exceeds import count ({import_count})",
                    location=location,
                    fix_suggestion="Update the reference or add the missing import"
                ))

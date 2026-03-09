"""
perforce.py — Perforce (P4) integration for the UE Level Editor.

Provides version control operations for UE assets:
- Checkout files before editing
- Checkin/submit changes
- Revert local modifications
- Sync/get latest
- File status queries

Requires the P4 command-line client to be installed and configured.
Uses P4CONFIG or environment variables for connection settings.
"""

import os
import re
import subprocess
import logging
from pathlib import Path
from typing import Optional, List, Dict, Tuple, Union
from dataclasses import dataclass
from enum import Enum

logger = logging.getLogger(__name__)


class P4Error(Exception):
    """Base exception for Perforce errors."""
    pass


class P4NotInstalledError(P4Error):
    """Raised when P4 client is not installed."""
    pass


class P4NotConnectedError(P4Error):
    """Raised when P4 connection fails."""
    pass


class P4FileNotInDepotError(P4Error):
    """Raised when a file is not in the depot."""
    pass


class P4CheckoutError(P4Error):
    """Raised when checkout fails."""
    pass


class FileStatus(Enum):
    """Perforce file status."""
    NOT_IN_DEPOT = "not_in_depot"
    SYNCED = "synced"
    CHECKED_OUT = "checked_out"
    CHECKED_OUT_BY_OTHER = "checked_out_by_other"
    OUT_OF_DATE = "out_of_date"
    NEEDS_RESOLVE = "needs_resolve"
    MARKED_FOR_ADD = "marked_for_add"
    MARKED_FOR_DELETE = "marked_for_delete"
    UNKNOWN = "unknown"


@dataclass
class P4FileInfo:
    """Information about a file in Perforce."""
    local_path: Path
    depot_path: Optional[str] = None
    status: FileStatus = FileStatus.UNKNOWN
    have_revision: Optional[int] = None
    head_revision: Optional[int] = None
    action: Optional[str] = None
    changelist: Optional[int] = None
    other_users: List[str] = None

    def __post_init__(self):
        if self.other_users is None:
            self.other_users = []

    @property
    def is_writable(self) -> bool:
        """Check if the file can be modified."""
        return self.status in (
            FileStatus.CHECKED_OUT,
            FileStatus.MARKED_FOR_ADD,
            FileStatus.NOT_IN_DEPOT,
        )

    @property
    def needs_checkout(self) -> bool:
        """Check if file needs to be checked out before editing."""
        return self.status == FileStatus.SYNCED


class PerforceClient:
    """
    Perforce client wrapper.

    Uses the P4 command-line tool. Connection settings are read from:
    1. P4CONFIG file (recommended for UE projects)
    2. Environment variables (P4PORT, P4USER, P4CLIENT)
    3. Explicit parameters

    Usage:
        p4 = PerforceClient()
        if p4.is_available():
            p4.checkout("/path/to/MyLevel.umap")
            # ... make changes ...
            p4.checkin("/path/to/MyLevel.umap", "Updated level logic")
    """

    def __init__(
        self,
        port: Optional[str] = None,
        user: Optional[str] = None,
        client: Optional[str] = None,
        password: Optional[str] = None,
    ):
        """
        Initialize Perforce client.

        Args:
            port: P4PORT (server:port). Uses env/P4CONFIG if not specified.
            user: P4USER. Uses env/P4CONFIG if not specified.
            client: P4CLIENT (workspace). Uses env/P4CONFIG if not specified.
            password: P4PASSWD. Uses env/P4CONFIG if not specified.
        """
        self._port = port
        self._user = user
        self._client = client
        self._password = password
        self._available: Optional[bool] = None
        self._connection_tested = False

    def _build_command(self, *args: str) -> List[str]:
        """Build P4 command with connection arguments."""
        cmd = ["p4"]

        if self._port:
            cmd.extend(["-p", self._port])
        if self._user:
            cmd.extend(["-u", self._user])
        if self._client:
            cmd.extend(["-c", self._client])
        if self._password:
            cmd.extend(["-P", self._password])

        # Use marshalled output for parsing
        cmd.append("-G")

        cmd.extend(args)
        return cmd

    def _run(self, *args: str, check: bool = True) -> List[Dict]:
        """
        Run a P4 command and return parsed output.

        Args:
            *args: P4 command arguments
            check: If True, raise on errors

        Returns:
            List of result dictionaries (P4 marshalled format)
        """
        import marshal

        cmd = self._build_command(*args)
        logger.debug(f"Running P4 command: {' '.join(cmd[:-1])} ...")  # Don't log -G

        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                timeout=30,
            )
        except FileNotFoundError:
            raise P4NotInstalledError(
                "P4 command-line client not found. "
                "Install from: https://www.perforce.com/downloads/helix-command-line-client-p4"
            )
        except subprocess.TimeoutExpired:
            raise P4Error("P4 command timed out")

        # Parse marshalled output
        results = []
        data = result.stdout
        offset = 0

        while offset < len(data):
            try:
                obj, new_offset = marshal.loads(data[offset:]), len(data)
                # marshal.loads doesn't return offset, need to handle differently
                break
            except:
                break

        # Fallback: re-run without -G for simpler parsing
        cmd_no_marshal = [c for c in cmd if c != "-G"]
        try:
            result = subprocess.run(
                cmd_no_marshal,
                capture_output=True,
                text=True,
                timeout=30,
            )
        except Exception as e:
            raise P4Error(f"P4 command failed: {e}")

        if check and result.returncode != 0:
            error_msg = result.stderr.strip() or result.stdout.strip()
            if "not connected" in error_msg.lower():
                raise P4NotConnectedError(f"P4 connection failed: {error_msg}")
            raise P4Error(f"P4 command failed: {error_msg}")

        return {"stdout": result.stdout, "stderr": result.stderr, "returncode": result.returncode}

    def _run_simple(self, *args: str) -> Tuple[str, str, int]:
        """Run P4 command without marshalling, return stdout/stderr/code."""
        cmd = ["p4"]

        if self._port:
            cmd.extend(["-p", self._port])
        if self._user:
            cmd.extend(["-u", self._user])
        if self._client:
            cmd.extend(["-c", self._client])

        cmd.extend(args)

        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=30,
            )
            return result.stdout, result.stderr, result.returncode
        except FileNotFoundError:
            raise P4NotInstalledError("P4 command-line client not found")
        except subprocess.TimeoutExpired:
            raise P4Error("P4 command timed out")

    def is_available(self) -> bool:
        """Check if P4 is installed and connection works."""
        if self._available is not None:
            return self._available

        try:
            stdout, stderr, code = self._run_simple("info")
            self._available = code == 0 and "Server address" in stdout
        except P4NotInstalledError:
            self._available = False
        except Exception:
            self._available = False

        return self._available

    def get_info(self) -> Dict[str, str]:
        """Get P4 connection info."""
        stdout, stderr, code = self._run_simple("info")

        info = {}
        for line in stdout.strip().split("\n"):
            if ": " in line:
                key, value = line.split(": ", 1)
                info[key.strip()] = value.strip()

        return info

    def get_file_status(self, file_path: Union[str, Path]) -> P4FileInfo:
        """
        Get the Perforce status of a file.

        Args:
            file_path: Path to the file

        Returns:
            P4FileInfo with status details
        """
        file_path = Path(file_path).resolve()
        info = P4FileInfo(local_path=file_path)

        # Check if file is in depot (fstat)
        stdout, stderr, code = self._run_simple("fstat", str(file_path))

        if code != 0 or "no such file" in stderr.lower() or "not in client" in stderr.lower():
            info.status = FileStatus.NOT_IN_DEPOT
            return info

        # Parse fstat output
        for line in stdout.strip().split("\n"):
            line = line.strip()
            if line.startswith("... depotFile "):
                info.depot_path = line.split(" ", 2)[2]
            elif line.startswith("... haveRev "):
                info.have_revision = int(line.split(" ")[2])
            elif line.startswith("... headRev "):
                info.head_revision = int(line.split(" ")[2])
            elif line.startswith("... action "):
                info.action = line.split(" ", 2)[2]
            elif line.startswith("... change "):
                try:
                    info.changelist = int(line.split(" ")[2])
                except ValueError:
                    pass
            elif line.startswith("... otherOpen"):
                match = re.search(r"otherOpen\d* (\S+)", line)
                if match:
                    info.other_users.append(match.group(1))

        # Determine status
        if info.action:
            if info.action == "edit":
                info.status = FileStatus.CHECKED_OUT
            elif info.action == "add":
                info.status = FileStatus.MARKED_FOR_ADD
            elif info.action == "delete":
                info.status = FileStatus.MARKED_FOR_DELETE
        elif info.other_users:
            info.status = FileStatus.CHECKED_OUT_BY_OTHER
        elif info.have_revision and info.head_revision:
            if info.have_revision < info.head_revision:
                info.status = FileStatus.OUT_OF_DATE
            else:
                info.status = FileStatus.SYNCED
        else:
            info.status = FileStatus.SYNCED

        return info

    def checkout(
        self,
        file_path: Union[str, Path],
        changelist: Optional[int] = None,
    ) -> P4FileInfo:
        """
        Check out a file for editing.

        Args:
            file_path: Path to the file
            changelist: Changelist number (default changelist if None)

        Returns:
            Updated P4FileInfo

        Raises:
            P4CheckoutError: If checkout fails
        """
        file_path = Path(file_path).resolve()

        # Check current status
        info = self.get_file_status(file_path)

        if info.status == FileStatus.NOT_IN_DEPOT:
            raise P4FileNotInDepotError(f"File not in depot: {file_path}")

        if info.status == FileStatus.CHECKED_OUT:
            logger.info(f"File already checked out: {file_path}")
            return info

        if info.status == FileStatus.CHECKED_OUT_BY_OTHER:
            raise P4CheckoutError(
                f"File checked out by: {', '.join(info.other_users)}"
            )

        # Perform checkout
        args = ["edit"]
        if changelist:
            args.extend(["-c", str(changelist)])
        args.append(str(file_path))

        stdout, stderr, code = self._run_simple(*args)

        if code != 0:
            raise P4CheckoutError(f"Checkout failed: {stderr or stdout}")

        logger.info(f"Checked out: {file_path}")
        return self.get_file_status(file_path)

    def checkout_if_needed(self, file_path: Union[str, Path]) -> P4FileInfo:
        """
        Check out a file only if it needs checkout.

        Safe to call even if P4 is not available - will just log a warning.

        Returns:
            P4FileInfo or None if P4 not available
        """
        if not self.is_available():
            logger.warning("P4 not available, skipping checkout")
            return None

        try:
            info = self.get_file_status(file_path)
            if info.needs_checkout:
                return self.checkout(file_path)
            return info
        except P4FileNotInDepotError:
            logger.debug(f"File not in depot, no checkout needed: {file_path}")
            return P4FileInfo(local_path=Path(file_path), status=FileStatus.NOT_IN_DEPOT)

    def add(
        self,
        file_path: Union[str, Path],
        changelist: Optional[int] = None,
    ) -> P4FileInfo:
        """
        Mark a new file for add.

        Args:
            file_path: Path to the file
            changelist: Changelist number

        Returns:
            Updated P4FileInfo
        """
        file_path = Path(file_path).resolve()

        args = ["add"]
        if changelist:
            args.extend(["-c", str(changelist)])
        args.append(str(file_path))

        stdout, stderr, code = self._run_simple(*args)

        if code != 0:
            raise P4Error(f"Add failed: {stderr or stdout}")

        logger.info(f"Marked for add: {file_path}")
        return self.get_file_status(file_path)

    def revert(
        self,
        file_path: Union[str, Path],
        keep_local: bool = False,
    ) -> P4FileInfo:
        """
        Revert changes to a file.

        Args:
            file_path: Path to the file
            keep_local: If True, keep local changes (revert -k)

        Returns:
            Updated P4FileInfo
        """
        file_path = Path(file_path).resolve()

        args = ["revert"]
        if keep_local:
            args.append("-k")
        args.append(str(file_path))

        stdout, stderr, code = self._run_simple(*args)

        if code != 0:
            raise P4Error(f"Revert failed: {stderr or stdout}")

        logger.info(f"Reverted: {file_path}")
        return self.get_file_status(file_path)

    def sync(
        self,
        file_path: Union[str, Path],
        force: bool = False,
    ) -> P4FileInfo:
        """
        Sync a file to the latest revision.

        Args:
            file_path: Path to the file
            force: Force sync even if file is open

        Returns:
            Updated P4FileInfo
        """
        file_path = Path(file_path).resolve()

        args = ["sync"]
        if force:
            args.append("-f")
        args.append(str(file_path))

        stdout, stderr, code = self._run_simple(*args)

        if code != 0 and "up-to-date" not in stdout.lower():
            raise P4Error(f"Sync failed: {stderr or stdout}")

        logger.info(f"Synced: {file_path}")
        return self.get_file_status(file_path)

    def submit(
        self,
        file_paths: Union[str, Path, List[Union[str, Path]]],
        description: str,
    ) -> int:
        """
        Submit changes to the depot.

        Args:
            file_paths: File(s) to submit
            description: Changelist description

        Returns:
            Changelist number
        """
        if isinstance(file_paths, (str, Path)):
            file_paths = [file_paths]

        file_paths = [str(Path(p).resolve()) for p in file_paths]

        # Create a changelist
        changelist_spec = f"Change: new\nDescription: {description}\nFiles:\n"
        for fp in file_paths:
            changelist_spec += f"\t{fp}\n"

        # For simplicity, use default changelist and submit directly
        args = ["submit", "-d", description] + file_paths

        stdout, stderr, code = self._run_simple(*args)

        if code != 0:
            raise P4Error(f"Submit failed: {stderr or stdout}")

        # Extract changelist number
        match = re.search(r"submitted as change (\d+)", stdout, re.IGNORECASE)
        if match:
            cl = int(match.group(1))
            logger.info(f"Submitted changelist {cl}")
            return cl

        logger.info(f"Submit completed: {stdout}")
        return 0

    def create_changelist(self, description: str) -> int:
        """
        Create a new pending changelist.

        Args:
            description: Changelist description

        Returns:
            Changelist number
        """
        # Create changelist spec
        spec = f"Change: new\nDescription:\n\t{description}\n"

        result = subprocess.run(
            ["p4", "change", "-i"],
            input=spec,
            capture_output=True,
            text=True,
        )

        if result.returncode != 0:
            raise P4Error(f"Failed to create changelist: {result.stderr}")

        match = re.search(r"Change (\d+) created", result.stdout)
        if match:
            return int(match.group(1))

        raise P4Error(f"Could not parse changelist number: {result.stdout}")


# Global default client instance
_default_client: Optional[PerforceClient] = None


def get_client() -> PerforceClient:
    """Get the default Perforce client instance."""
    global _default_client
    if _default_client is None:
        _default_client = PerforceClient()
    return _default_client


def checkout_for_edit(file_path: Union[str, Path]) -> Optional[P4FileInfo]:
    """
    Convenience function: checkout a file if P4 is available.

    Safe to call even without P4 - will return None.
    """
    return get_client().checkout_if_needed(file_path)


def is_p4_available() -> bool:
    """Check if Perforce is available."""
    return get_client().is_available()

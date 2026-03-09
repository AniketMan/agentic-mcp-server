"""
Unit tests for core/perforce.py

Tests for Perforce integration (mocked - no actual P4 server required).
"""
import os
import subprocess
import pytest
from unittest.mock import Mock, patch, MagicMock
from pathlib import Path

import sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from core.perforce import (
    PerforceClient,
    P4FileInfo,
    FileStatus,
    is_p4_available,
    get_client,
    checkout_for_edit,
)


class TestP4FileInfo:
    """Tests for P4FileInfo dataclass."""

    def test_create_file_info(self):
        """Test creating a P4FileInfo object."""
        info = P4FileInfo(
            depot_path="//depot/project/file.umap",
            local_path="/local/project/file.umap",
            status=FileStatus.CHECKED_OUT,
            have_rev=5,
            head_rev=5,
            action="edit",
        )
        assert info.depot_path == "//depot/project/file.umap"
        assert info.status == FileStatus.CHECKED_OUT
        assert info.have_rev == 5

    def test_file_status_enum(self):
        """Test FileStatus enum values."""
        assert FileStatus.NOT_IN_DEPOT is not None
        assert FileStatus.SYNCED is not None
        assert FileStatus.CHECKED_OUT is not None
        assert FileStatus.NEEDS_SYNC is not None
        assert FileStatus.NEEDS_RESOLVE is not None


class TestPerforceClient:
    """Tests for PerforceClient class."""

    def test_init(self):
        """Test client initialization."""
        client = PerforceClient()
        assert client is not None

    @patch("subprocess.run")
    def test_run_p4_command_success(self, mock_run):
        """Test running a successful P4 command."""
        mock_run.return_value = MagicMock(
            returncode=0,
            stdout="... depotFile //depot/test.txt\n",
            stderr=""
        )

        client = PerforceClient()
        success, output, error = client._run_p4_command(["fstat", "test.txt"])

        assert success is True
        assert "depotFile" in output
        assert error == ""

    @patch("subprocess.run")
    def test_run_p4_command_failure(self, mock_run):
        """Test running a failed P4 command."""
        mock_run.return_value = MagicMock(
            returncode=1,
            stdout="",
            stderr="No such file"
        )

        client = PerforceClient()
        success, output, error = client._run_p4_command(["fstat", "nonexistent.txt"])

        assert success is False
        assert "No such file" in error

    @patch("subprocess.run")
    def test_run_p4_command_timeout(self, mock_run):
        """Test P4 command timeout handling."""
        mock_run.side_effect = subprocess.TimeoutExpired(cmd="p4", timeout=30)

        client = PerforceClient()
        success, output, error = client._run_p4_command(["fstat", "test.txt"])

        assert success is False
        assert "timeout" in error.lower()

    @patch.object(PerforceClient, "_run_p4_command")
    def test_checkout_success(self, mock_run):
        """Test successful checkout."""
        mock_run.return_value = (True, "//depot/test.umap#5 - opened for edit", "")

        client = PerforceClient()
        result = client.checkout("/local/test.umap")

        assert result is not None
        mock_run.assert_called()

    @patch.object(PerforceClient, "_run_p4_command")
    def test_checkout_already_checked_out(self, mock_run):
        """Test checkout when file is already checked out."""
        mock_run.return_value = (False, "", "already opened for edit")

        client = PerforceClient()
        result = client.checkout("/local/test.umap")

        assert result is not None

    @patch.object(PerforceClient, "_run_p4_command")
    def test_checkout_file_not_in_depot(self, mock_run):
        """Test checkout when file is not in depot."""
        mock_run.return_value = (False, "", "not on client")

        client = PerforceClient()
        result = client.checkout("/local/test.umap")

        assert result is None

    @patch.object(PerforceClient, "get_file_status")
    @patch.object(PerforceClient, "checkout")
    def test_checkout_if_needed_synced_file(self, mock_checkout, mock_status):
        """Test checkout_if_needed on a synced file."""
        mock_status.return_value = P4FileInfo(
            depot_path="//depot/test.umap",
            local_path="/local/test.umap",
            status=FileStatus.SYNCED,
            have_rev=5,
            head_rev=5,
        )
        mock_checkout.return_value = mock_status.return_value

        client = PerforceClient()
        result = client.checkout_if_needed("/local/test.umap")

        mock_checkout.assert_called_once()

    @patch.object(PerforceClient, "get_file_status")
    @patch.object(PerforceClient, "checkout")
    def test_checkout_if_needed_already_checked_out(self, mock_checkout, mock_status):
        """Test checkout_if_needed on already checked out file."""
        mock_status.return_value = P4FileInfo(
            depot_path="//depot/test.umap",
            local_path="/local/test.umap",
            status=FileStatus.CHECKED_OUT,
            have_rev=5,
            head_rev=5,
            action="edit",
        )

        client = PerforceClient()
        result = client.checkout_if_needed("/local/test.umap")

        mock_checkout.assert_not_called()
        assert result.status == FileStatus.CHECKED_OUT

    @patch.object(PerforceClient, "_run_p4_command")
    def test_revert_success(self, mock_run):
        """Test successful revert."""
        mock_run.return_value = (True, "//depot/test.umap#5 - was edit, reverted", "")

        client = PerforceClient()
        result = client.revert("/local/test.umap")

        assert result is True

    @patch.object(PerforceClient, "_run_p4_command")
    def test_add_success(self, mock_run):
        """Test successful add."""
        mock_run.return_value = (True, "//depot/test.umap#1 - opened for add", "")

        client = PerforceClient()
        result = client.add("/local/test.umap")

        assert result is not None

    @patch.object(PerforceClient, "_run_p4_command")
    def test_sync_success(self, mock_run):
        """Test successful sync."""
        mock_run.return_value = (True, "//depot/test.umap#5 - updating /local/test.umap", "")

        client = PerforceClient()
        result = client.sync("/local/test.umap")

        assert result is True

    @patch.object(PerforceClient, "_run_p4_command")
    def test_get_file_status(self, mock_run):
        """Test getting file status."""
        mock_run.return_value = (
            True,
            "... depotFile //depot/test.umap\n"
            "... clientFile /local/test.umap\n"
            "... haveRev 5\n"
            "... headRev 5\n",
            ""
        )

        client = PerforceClient()
        result = client.get_file_status("/local/test.umap")

        assert result is not None
        assert result.depot_path == "//depot/test.umap"
        assert result.have_rev == 5


class TestModuleFunctions:
    """Tests for module-level functions."""

    @patch("subprocess.run")
    def test_is_p4_available_true(self, mock_run):
        """Test is_p4_available when P4 is available."""
        mock_run.return_value = MagicMock(returncode=0)

        result = is_p4_available()
        assert result is True

    @patch("subprocess.run")
    def test_is_p4_available_false(self, mock_run):
        """Test is_p4_available when P4 is not available."""
        mock_run.side_effect = FileNotFoundError()

        result = is_p4_available()
        assert result is False

    def test_get_client_returns_singleton(self):
        """Test that get_client returns a singleton."""
        client1 = get_client()
        client2 = get_client()
        assert client1 is client2

    @patch("core.perforce.get_client")
    def test_checkout_for_edit_calls_checkout_if_needed(self, mock_get_client):
        """Test checkout_for_edit convenience function."""
        mock_client = Mock()
        mock_get_client.return_value = mock_client

        checkout_for_edit("/local/test.umap")

        mock_client.checkout_if_needed.assert_called_once()


class TestFileStatus:
    """Tests for FileStatus enum."""

    def test_all_statuses_exist(self):
        """Test that all expected statuses exist."""
        statuses = [
            FileStatus.NOT_IN_DEPOT,
            FileStatus.SYNCED,
            FileStatus.CHECKED_OUT,
            FileStatus.NEEDS_SYNC,
            FileStatus.NEEDS_RESOLVE,
            FileStatus.DELETED,
        ]
        assert len(statuses) == 6


if __name__ == "__main__":
    pytest.main([__file__, "-v"])

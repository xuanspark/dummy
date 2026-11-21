from app.core.hardware_interface import (
    FIRMWARE_BASELINE_VERSION,
    FIRMWARE_CANDIDATE_VERSION,
    HARDWARE_INTERFACE_VERSION,
    describe_hardware,
)


def test_hardware_interface_is_versioned_and_read_only():
    contract = describe_hardware(connected=False, motion_state="DISCONNECTED")

    assert contract["interface_version"] == HARDWARE_INTERFACE_VERSION
    assert contract["firmware"]["baseline"] == FIRMWARE_BASELINE_VERSION
    assert contract["firmware"]["candidate"] == FIRMWARE_CANDIDATE_VERSION
    assert contract["connection"] == {
        "connected": False,
        "motion_state": "DISCONNECTED",
    }
    assert contract["nodes"]["joints"]["ids"] == [1, 2, 3, 4, 5, 6]
    assert "#STATUS" in contract["commands"]["diagnostics"]
    assert contract["validation"]["pending"]


def test_hardware_interface_preserves_unknown_capability_rule():
    contract = describe_hardware(connected=True, motion_state="NORMAL")

    assert "unknown_capability" in contract["compatibility"]
    assert contract["connection"]["connected"] is True

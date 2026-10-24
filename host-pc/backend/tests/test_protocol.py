"""Unit tests for the IO-free protocol layer."""
from __future__ import annotations

import pytest

from app.core import protocol as P


# ---------- response classification ----------

class TestParseResponse:
    def test_simple_ok(self):
        r = P.parse_response("Move OK")
        assert r is not None
        assert r.tag == "Move" and r.ok and r.rest == ""

    def test_ok_with_rest(self):
        r = P.parse_response("GetJPos OK 0.0 -75.0 180.0 0.0 0.0 0.0 H:0")
        assert r and r.ok and r.tag == "GetJPos"
        assert r.rest == "0.0 -75.0 180.0 0.0 0.0 0.0 H:0"

    def test_error(self):
        r = P.parse_response("Move Error: limit J1 Max")
        assert r and not r.ok and r.tag == "Move"
        assert r.error == "limit J1 Max"

    def test_blank(self):
        assert P.parse_response("") is None
        assert P.parse_response("   ") is None

    def test_unstructured(self):
        # Boot banners / async logs without OK/Error suffix
        assert P.parse_response("[INFO] something happened") is None
        assert P.parse_response("Robot booting...") is None

    def test_underscore_tag(self):
        r = P.parse_response("HD_O OK")
        assert r and r.ok and r.tag == "HD_O"

    def test_strips_whitespace(self):
        r = P.parse_response("  Reset OK   ")
        assert r and r.ok and r.tag == "Reset"


# ---------- joint pos parsing ----------

class TestParseJpos:
    def test_with_hand(self):
        jp = P.parse_jpos("0.0 -75.0 180.0 0.0 0.0 0.0 H:0")
        assert jp is not None
        assert jp.j1 == 0.0 and jp.j2 == -75.0 and jp.j3 == 180.0
        assert jp.j6 == 0.0 and jp.hand == 0

    def test_without_hand(self):
        jp = P.parse_jpos("1 2 3 4 5 6")
        assert jp is not None and jp.hand == -1

    def test_comma_separated(self):
        jp = P.parse_jpos("1,2,3,4,5,6,H:1")
        assert jp is not None and jp.hand == 1 and jp.j2 == 2.0

    def test_too_few(self):
        assert P.parse_jpos("1 2 3") is None


# ---------- cart pos parsing ----------

def test_parse_lpos():
    lp = P.parse_lpos("100.5 -50.2 250.0 0.0 0.0 0.0 H:0")
    assert lp is not None
    assert lp.x == 100.5 and lp.z == 250.0 and lp.hand == 0


# ---------- command builders ----------

class TestBuilders:
    def test_move_j(self):
        s = P.cmd_move_j([0, -75, 90, 0, 0, 0], 10)
        assert s.startswith("&") and s.endswith(",10")
        # Must have 6 commas before speed (7 comma-separated fields total)
        assert len(s.split(",")) == 7

    def test_move_j_without_speed(self):
        # 6-arg variant — firmware uses its current joint speed
        s = P.cmd_move_j([0, 0, 134, 35, 66, 0])
        assert s.startswith("&") and "," in s
        # Exactly 6 comma-separated fields (no trailing speed)
        assert len(s.split(",")) == 6
        # User-provided example should round-trip
        assert s == "&0.000,0.000,134.000,35.000,66.000,0.000"

    def test_move_j_wrong_len(self):
        with pytest.raises(ValueError):
            P.cmd_move_j([1, 2, 3], 10)

    def test_move_l(self):
        s = P.cmd_move_l([100, 50, 200, 0, 0, 0], 5)
        assert s.startswith("@") and s.endswith(",5")

    def test_move_l_without_speed(self):
        # User-provided example: @175,44,160,-135,-13.54,-14.6
        s = P.cmd_move_l([175, 44, 160, -135, -13.54, -14.6])
        assert s.startswith("@")
        assert len(s.split(",")) == 6
        # No trailing speed field
        assert not s.rstrip("0").rstrip(".").endswith(",")

    def test_hand_pos_range(self):
        assert P.cmd_hand_pos(0) == "!HD_POS 0"
        assert P.cmd_hand_pos(100) == "!HD_POS 100"
        with pytest.raises(ValueError):
            P.cmd_hand_pos(-1)
        with pytest.raises(ValueError):
            P.cmd_hand_pos(101)

    def test_rgb_mode_range(self):
        assert P.cmd_rgb_mode(11) == "!RGB_MODE 11"
        with pytest.raises(ValueError):
            P.cmd_rgb_mode(12)

    def test_rgb_custom(self):
        assert P.cmd_rgb_custom(255, 128, 0) == "!RGB_CUSTOM 255 128 0"
        with pytest.raises(ValueError):
            P.cmd_rgb_custom(256, 0, 0)


# ---------- expected_tag mapping ----------

class TestExpectedTag:
    def test_simple(self):
        assert P.expected_tag("!HOME") == "Home"
        assert P.expected_tag("!CLOSE") == "Close"

    def test_with_args(self):
        assert P.expected_tag("!HD_POS 50") == "HD_POS"
        assert P.expected_tag("!RGB_CUSTOM 1 2 3") == "RGB_CUSTOM"

    def test_move_prefixes(self):
        assert P.expected_tag("&0,0,0,0,0,0,10") == "Move"
        assert P.expected_tag("@1,2,3,4,5,6,10") == "Move"
        # `>` is a firmware-side alias of `&` (see dummy_robot.cpp ParseCommand).
        assert P.expected_tag(">0,0,0,0,0,0") == "Move"

    def test_unknown_raises(self):
        import pytest
        with pytest.raises(KeyError):
            P.expected_tag("!NOPE")

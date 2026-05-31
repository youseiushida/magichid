"""Report table loader (single source of truth, generated from hid_descriptor.h).

reports.json is produced by tools/gen_reports.py. This module exposes lookups by
Report ID and by name so the client can validate payload lengths and resolve names.
"""
import json
import os

_JSON = os.path.join(os.path.dirname(__file__), "..", "reports.json")


class ReportInfo:
    __slots__ = ("id", "name", "page", "input_bytes", "output_bytes", "feature_bytes")

    def __init__(self, d):
        self.id = d["id"]
        self.name = d["name"]
        self.page = d["page"]
        self.input_bytes = d["input_bytes"]
        self.output_bytes = d["output_bytes"]
        self.feature_bytes = d["feature_bytes"]

    def __repr__(self):
        return (f"ReportInfo(id={self.id}, name={self.name}, page=0x{self.page:02X}, "
                f"in={self.input_bytes}, out={self.output_bytes}, feat={self.feature_bytes})")


def load_reports(path=None):
    path = path or _JSON
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    by_id, by_name = {}, {}
    for d in data["reports"]:
        info = ReportInfo(d)
        by_id[info.id] = info
        by_name[info.name] = info
        # also accept the short name without the REPORT_ID_ prefix
        if info.name.startswith("REPORT_ID_"):
            by_name[info.name[len("REPORT_ID_"):]] = info
    return by_id, by_name


# Module-level convenience (loaded once).
try:
    BY_ID, BY_NAME = load_reports()
except FileNotFoundError:
    BY_ID, BY_NAME = {}, {}


def resolve(report) -> int:
    """Accept an int id or a name (with or without REPORT_ID_ prefix) -> int id."""
    if isinstance(report, int):
        return report
    info = BY_NAME.get(report) or BY_NAME.get(f"REPORT_ID_{report}")
    if info is None:
        raise KeyError(f"unknown report: {report!r}")
    return info.id

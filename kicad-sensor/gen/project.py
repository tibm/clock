"""Write the sensor-board project files (.kicad_pro, lib tables, .kicad_sch).

The .kicad_pro body and the sheet serializer are the main board's
(`../../kicad/gen/project2.py`) with its output directory re-pointed here, so
both boards stay on one KiCad-format writer.  Only the library tables differ:
this project's custom symbols live in `gen/sensor_custom.kicad_sym` under the
nickname `sensor`, and it has no custom footprint library (yet).
"""
from __future__ import annotations
import os

import project2

OUT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def write_sym_lib_table():
    txt = (
        "(sym_lib_table\n"
        "\t(version 7)\n"
        '\t(lib (name "sensor")(type "KiCad")(uri "${KIPRJMOD}/gen/sensor_custom.kicad_sym")(options "")(descr "Clock sensor-board custom symbols"))\n'
        ")\n"
    )
    path = os.path.join(OUT_DIR, "sym-lib-table")
    with open(path, "w") as f:
        f.write(txt)
    return path


def write_fp_lib_table():
    """`sensor` = the one custom land pattern (BNO085); everything else is stock."""
    txt = (
        "(fp_lib_table\n"
        "\t(version 7)\n"
        '\t(lib (name "sensor")(type "KiCad")(uri "${KIPRJMOD}/sensor.pretty")(options "")(descr "Clock sensor-board custom footprints"))\n'
        ")\n"
    )
    path = os.path.join(OUT_DIR, "fp-lib-table")
    with open(path, "w") as f:
        f.write(txt)
    return path


def write_project(name):
    project2.OUT_DIR = OUT_DIR
    return project2.write_project(name)


def write_sheet(sch, filename):
    project2.OUT_DIR = OUT_DIR
    return project2.write_sheet(sch, filename)

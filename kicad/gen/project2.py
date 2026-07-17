"""Write the kicad2 project files (.kicad_pro, sym-lib-table, .kicad_sch)."""
from __future__ import annotations
import json
import os

OUT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def write_project(name):
    pro = {
        "board": {"design_settings": {"defaults": {}}, "layer_presets": [],
                  "viewports": []},
        "boards": [],
        "cvpcb": {"equivalence_files": []},
        "erc": {"erc_exclusions": [], "meta": {"version": 0},
                "rule_severities": {}, "rule_severities_notes": {}},
        "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
        "meta": {"filename": f"{name}.kicad_pro", "version": 3},
        "net_settings": {
            "classes": [{
                "name": "Default", "bus_width": 12, "clearance": 0.2,
                "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25,
                "diff_pair_width": 0.2, "line_style": 0,
                "microvia_diameter": 0.3, "microvia_drill": 0.1,
                "pcb_color": "rgba(0, 0, 0, 0.000)",
                "schematic_color": "rgba(0, 0, 0, 0.000)",
                "track_width": 0.25, "via_diameter": 0.8, "via_drill": 0.4,
                "wire_width": 6}],
            "meta": {"version": 4}},
        "pcbnew": {"last_paths": {}, "page_layout_descr_file": ""},
        "schematic": {
            "annotate_start_num": 0,
            "bom_export_filename": "",
            "bom_fmt_presets": [], "bom_fmt_settings": {}, "bom_presets": [],
            "bom_settings": {},
            "connection_grid_size": 50.0,
            "drawing": {"dashed_lines_dash_length_ratio": 12.0,
                        "dashed_lines_gap_length_ratio": 3.0,
                        "default_line_thickness": 6.0,
                        "default_text_size": 50.0,
                        "field_names": [],
                        "intersheets_ref_own_page": False,
                        "intersheets_ref_prefix": "",
                        "intersheets_ref_short": False,
                        "intersheets_ref_show": False,
                        "intersheets_ref_suffix": "",
                        "junction_size_choice": 3,
                        "label_size_ratio": 0.375,
                        "operating_point_overlay": False,
                        "overbar_offset_ratio": 1.23,
                        "pin_symbol_size": 25.0,
                        "text_offset_ratio": 0.15},
            "legacy_lib_dir": "", "legacy_lib_list": [],
            "meta": {"version": 1},
            "net_format_name": "",
            "ngspice": {},
            "page_layout_descr_file": "",
            "plot_directory": "",
            "spice_current_sheet_as_root": False,
            "spice_external_command": "",
            "spice_model_current_sheet_as_root": True,
            "spice_save_all_currents": False,
            "spice_save_all_dissipations": False,
            "spice_save_all_voltages": False,
            "subpart_first_id": 65,
            "subpart_id_separator": 0},
        "sheets": [],
        "text_variables": {},
    }
    path = os.path.join(OUT_DIR, f"{name}.kicad_pro")
    if os.path.exists(path):
        # never clobber the project file once KiCad has saved its own
        # settings into it (ERC config, board setup, ...)
        return path
    with open(path, "w") as f:
        json.dump(pro, f, indent=2)
    return path


def write_sym_lib_table():
    txt = (
        "(sym_lib_table\n"
        "\t(version 7)\n"
        '\t(lib (name "clock")(type "KiCad")(uri "${KIPRJMOD}/gen/clock_custom.kicad_sym")(options "")(descr "Clock project custom symbols"))\n'
        ")\n"
    )
    path = os.path.join(OUT_DIR, "sym-lib-table")
    with open(path, "w") as f:
        f.write(txt)
    return path


def write_sheet(sch, filename):
    path = os.path.join(OUT_DIR, filename)
    with open(path, "w") as f:
        f.write(sch.render())
    return path

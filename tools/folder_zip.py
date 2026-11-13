#!/usr/bin/env python3
"""Make a "just the folder" zip from a release zip, for MPC OS 3.10.1 and later.

A release zip holds <Name>-<ver>/portable/<Vendor> - VST - <Name>/ plus install scripts. On 3.10.1 a user only needs that
plugin folder, copied into a `VST2 Plugins` folder, so this writes a zip whose only top-level entry is the plugin folder:

    tools/folder_zip.py Plugin-Manager-G2-1.1.1-mpc-armv7.zip OpenMPC-Plugin-Manager-1.1.1-Gen1.zip

The plugin folder must contain plugin.json (MPC OS 3.10.1 needs it). Bytes and file modes are copied unchanged.
Standard library only.
"""
import sys
import zipfile


def folder_zip(src, dst):
    with zipfile.ZipFile(src) as z:
        names = z.namelist()
        roots = sorted({n.split("/")[2] for n in names if n.count("/") >= 3 and n.split("/")[1] == "portable" and n.split("/")[2]})
        if len(roots) != 1:
            raise SystemExit("%s: expected one folder in portable/, found %r" % (src, roots))
        folder = roots[0]
        prefix = next(n for n in names if n.split("/")[1:3] == ["portable", folder]).split("/")[0] + "/portable/"
        if prefix + folder + "/plugin.json" not in names:
            raise SystemExit("%s: %s has no plugin.json (not ready for MPC OS 3.10.1)" % (src, folder))
        with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as out:
            for info in z.infolist():
                if not info.filename.startswith(prefix + folder + "/"):
                    continue
                rel = info.filename[len(prefix):]
                if ".." in rel.split("/"):
                    raise SystemExit("unsafe name: " + info.filename)
                new = zipfile.ZipInfo(rel, info.date_time)
                new.external_attr, new.compress_type, new.create_system = info.external_attr, zipfile.ZIP_DEFLATED, info.create_system
                out.writestr(new, z.read(info.filename))
    return folder


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    print("%s -> %s (%s)" % (sys.argv[1], sys.argv[2], folder_zip(sys.argv[1], sys.argv[2])))

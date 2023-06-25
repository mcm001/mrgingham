import requests
import tempfile
import zipfile
import argparse

# https://stackoverflow.com/a/12514470
import os, shutil
def copytree(src, dst, symlinks=False, ignore=None):
    for item in os.listdir(src):
        s = os.path.join(src, item)
        d = os.path.join(dst, item)
        if os.path.isdir(s):
            shutil.copytree(s, d, symlinks, ignore, dirs_exist_ok=True)
        else:
            shutil.copy2(s, d)

def download_and_extract(file_name, dir):

    url = f"https://frcmaven.wpi.edu/artifactory/release/edu/wpi/first/thirdparty/{YEAR}/opencv/opencv-cpp/{VERSION}/{file_name}"
    print("Downloading from " + url)
    req = requests.get(url, allow_redirects=True)

    with tempfile.TemporaryFile() as fp:
        fp.write(req.content)

        with tempfile.TemporaryDirectory() as tempdir:
            zipfile.ZipFile(fp).extractall(tempdir)
            copytree(tempdir, dir)



if __name__ == "__main__":

    parser = argparse.ArgumentParser(description='Download wpilib opencv from maven')
    parser.add_argument('--arch', required=True, help="Architecture (eg linuxx86-64)")
    parser.add_argument('--static', help="Set if we should download static. Unset for dynamic", action='store_true')
    parser.add_argument('--debug', help="Set if we should download debug libraries. Unset for release", action='store_true')
    parser.add_argument('--year', required=True, type=str, help="FRC year (eg 'frc2023')")
    parser.add_argument('--version', required=True, type=str, help="OpenCV version (eg '4.6.0-5')")

    args = parser.parse_args()
    print(args)

    ARCH_NAME = "linuxx86-64"
    LIB_TYPE = "static" # "" or "static"
    DEBUG_TYPE = "" # "" or "debug"

    YEAR="frc2023"
    VERSION="4.6.0-5"

    lib_name = f"opencv-cpp-{VERSION}-{ARCH_NAME}{LIB_TYPE}{DEBUG_TYPE}.zip"
    header_name = f"opencv-cpp-{VERSION}-headers.zip"

    download_and_extract(lib_name, "test")
    download_and_extract(header_name, "test")

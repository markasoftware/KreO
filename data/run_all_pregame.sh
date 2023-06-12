set -e

python3.8 pregame.py out/kreo-libbmp.json
python3.8 pregame.py out/kreo-optparse.json
python3.8 pregame.py out/kreo-ser4cpp.json
python3.8 pregame.py out/kreo-tinyxml2.json

python3.8 pregame.py out/lego-libbmp.json
python3.8 pregame.py out/lego-optparse.json
python3.8 pregame.py out/lego-ser4cpp.json
python3.8 pregame.py out/lego-tinyxml2.json

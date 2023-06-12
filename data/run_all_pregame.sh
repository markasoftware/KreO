set -e

python3.8 pregame.py data/kreo-libbmp.json
python3.8 pregame.py data/kreo-optparse.json
python3.8 pregame.py data/kreo-ser4cpp.json
python3.8 pregame.py data/kreo-tinyxml2.json

python3.8 pregame.py data/lego-libbmp.json
python3.8 pregame.py data/lego-optparse.json
python3.8 pregame.py data/lego-ser4cpp.json
python3.8 pregame.py data/lego-tinyxml2.json

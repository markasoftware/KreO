all: pregame game

windows: game

pregame:
	$(MAKE) -C pregame

game:
	$(MAKE) -C pintool

.PHONY: pregame game

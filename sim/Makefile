PYTHON ?= python
CC ?= cc

run:
	$(PYTHON) run.py --cc "$(CC)"

check:
	$(PYTHON) ../tools/test_tank_backup.py --cc "$(CC)"
	$(PYTHON) -m unittest discover -s ../tools -p "test_*.py"
	$(PYTHON) run.py --cc "$(CC)" --smoke

.PHONY: run check

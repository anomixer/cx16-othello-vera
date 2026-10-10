PYTHON ?= python3

.PHONY: all game site serve clean
all: game

game:
	$(MAKE) -C src

site: game
	$(PYTHON) tools/build_site.py

serve: site
	$(PYTHON) -m http.server 8000 --directory build/site

clean:
	$(MAKE) -C src clean
	rm -rf build/site

SRCDIR ?= /7/gtk/epiphany/third-party/readability/
JS2H ?= ~/extern/vimb-master/src/scripts/js2h.sh

%:$(SRCDIR)/%
	cpp $^ | sed -e '/^#/d' > $@

readability.h: Readability.js reader.css
	$(JS2H) Readability.js > readability.h
	$(JS2H) reader.css >> readability.h

clean:
	rm -fv Readability.js style.css readability.h


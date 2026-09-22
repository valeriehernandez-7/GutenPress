# Top-level Makefile for GutenPress.
#
# Orchestrates the per-module Makefiles under src/ so the whole project can
# be built, tested and cleaned from the repository root. Build rules stay
# inside each module; this file only delegates to them.

SRC_DIR      = src
COMMON_DIR   = common
VERSION_DIRS = serial fork_ipc pthreads
GUI_DIR      = gui
GUI_BINARY   = gutenpress

# Test input for the round-trip check. Override to use another dataset:
#   make check CORPUS_DIR=path/to/other/texts
CORPUS_DIR   = $(SRC_DIR)/gutenberg_corpus
TEST_DIR     = test_output

# Each entry is <module folder>:<binary prefix>, since binary names do not
# always match their folder (fork_ipc -> fork_*, pthreads -> pthread_*).
VERSION_BINARIES = serial:serial fork_ipc:fork pthreads:pthread

.PHONY: all versions $(VERSION_DIRS) gui tests check run clean distclean help

all: versions gui

versions: $(VERSION_DIRS)

$(VERSION_DIRS):
	$(MAKE) -C $(SRC_DIR)/$@

# The GUI launches the six compressor/decompressor binaries as
# subprocesses, so they must be built before the GUI is usable.
gui: versions
	$(MAKE) -C $(SRC_DIR)/$(GUI_DIR)

tests:
	$(MAKE) -C $(SRC_DIR)/$(COMMON_DIR)

# Round trip for every version: compress the corpus, extract it again and
# require the result to be byte-identical to the original (diff -rq).
check: versions
	@test -d $(CORPUS_DIR) || { echo "Corpus not found: $(CORPUS_DIR)"; exit 1; }
	@mkdir -p $(TEST_DIR)
	@set -e; for entry in $(VERSION_BINARIES); do \
		dir=$${entry%%:*}; prefix=$${entry##*:}; \
		archive=$(TEST_DIR)/$$dir.huff; extracted=$(TEST_DIR)/$${dir}_extracted; \
		rm -rf $$extracted; \
		echo "==> [$$dir] compressing $(CORPUS_DIR)"; \
		./$(SRC_DIR)/$$dir/$${prefix}_compressor $(CORPUS_DIR) $$archive; \
		echo "==> [$$dir] decompressing $$archive"; \
		./$(SRC_DIR)/$$dir/$${prefix}_decompressor $$archive $$extracted; \
		diff -rq $(CORPUS_DIR) $$extracted; \
		echo "==> [$$dir] OK: output identical to corpus"; \
	done
	@echo "All versions passed the round-trip check."

# Run from the GUI folder so relative paths (icons, style.css, binaries)
# resolve the same way they do during development.
run: all
	cd $(SRC_DIR)/$(GUI_DIR) && ./$(GUI_BINARY)

clean:
	@for dir in $(COMMON_DIR) $(VERSION_DIRS) $(GUI_DIR); do \
		$(MAKE) -C $(SRC_DIR)/$$dir clean || exit 1; \
	done

# common/ and gui/ have no distclean target, so they only get clean.
distclean:
	rm -rf $(TEST_DIR)
	@for dir in $(VERSION_DIRS); do \
		$(MAKE) -C $(SRC_DIR)/$$dir distclean || exit 1; \
	done
	@for dir in $(COMMON_DIR) $(GUI_DIR); do \
		$(MAKE) -C $(SRC_DIR)/$$dir clean || exit 1; \
	done

help:
	@echo "Available targets:"
	@echo "  make            Build all versions and the GUI"
	@echo "  make versions   Build serial, fork_ipc and pthreads only"
	@echo "  make serial     Build one version (also: fork_ipc, pthreads)"
	@echo "  make gui        Build the GUI (builds the versions first)"
	@echo "  make tests      Build the smoke tests in src/common"
	@echo "  make check      Round-trip test of every version on the corpus"
	@echo "  make run        Build everything and launch the GUI"
	@echo "  make clean      Remove compiled binaries"
	@echo "  make distclean  Remove binaries and test artifacts"
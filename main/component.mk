#
# "main" pseudo-component.
# Default behaviour: compile every *.c in this directory, add ./include to path.
#

# Web panel: web/ is built (Svelte + Vite) into one gzipped HTML file that is
# committed at www/index.html.gz and embedded as
# _binary_index_html_gz_start / _binary_index_html_gz_end.
COMPONENT_EMBED_FILES := www/index.html.gz

# Rebuild the panel only when its sources are newer and bun is installed;
# otherwise the committed .gz is embedded as-is.
WEB_DIR := $(abspath $(COMPONENT_PATH)/../web)
ifneq ($(shell command -v bun 2>/dev/null),)
$(COMPONENT_PATH)/www/index.html.gz: $(shell find $(WEB_DIR)/src -type f) \
		$(addprefix $(WEB_DIR)/,index.html package.json bun.lock vite.config.js)
	cd $(WEB_DIR) && bun install --frozen-lockfile && bun run build || \
		{ echo "WARNING: web build failed, embedding committed main/www/index.html.gz" >&2; touch $@; }
endif

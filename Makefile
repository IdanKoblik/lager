.PHONY: all module clean compile_commands prep

KDIR ?= /lib/modules/$(shell uname -r)/build
JOBS ?= $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
PWD  := $(shell pwd)
SRC  := $(PWD)/kernel
OUT  := $(PWD)/build

all: module

prep:
	@mkdir -p $(OUT)
	@for f in $(SRC)/*; do ln -sf $$f $(OUT)/$$(basename $$f); done

module: prep
	$(MAKE) -j$(JOBS) -C $(KDIR) M=$(OUT) modules

compile_commands: prep
	$(MAKE) -C $(KDIR) M=$(OUT) clean
	bear --output compile_commands.json -- $(MAKE) -j$(JOBS) -C $(KDIR) M=$(OUT) modules
	@python3 -c 'import json,os;p="compile_commands.json";d=json.load(open(p));[e.update({"file":os.path.realpath(os.path.join(e["directory"],e["file"]))}) for e in d];json.dump(d,open(p,"w"),indent=2)'

clean:
	$(RM) -r $(OUT)

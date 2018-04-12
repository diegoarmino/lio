################################################################################
INCLUDES :=
INCLUDES += subm_smat.o

OBJECTS += $(INCLUDES)
#$(OBJPATH)/faint_cpu.o : $(INCLUDES) faint_cpu.mk
$(OBJPATH)/faint_cpu.o : $(INCLUDES:%.o=$(OBJPATH)/%.o) ibos.mk
################################################################################

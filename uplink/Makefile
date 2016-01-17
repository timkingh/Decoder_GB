SUBDIRS=GB28181


RECURSIVE_MAKE= @for subdir in $(SUBDIRS); \
	do \
        echo "making in $$subdir"; \
        cd $$subdir; \
        make arm; \
        cd ..; \
        done  

RECURSIVE_I386= @for subdir in $(SUBDIRS); \
	do \
        echo "I386 in $$subdir"; \
        cd $$subdir; \
        make i386; \
        cd ..; \
        done

 RECURSIVE_INSTALL= @for subdir in $(SUBDIRS); \
	do \
        echo "installing in $$subdir"; \
        cd $$subdir; \
        make install; \
        cd ..; \
        done
        
RECURSIVE_CLEAN= @for subdir in $(SUBDIRS); \
	do \
        echo "cleaning in $$subdir"; \
        cd $$subdir; \
        make clean; \
        cd ..; \
        done  	
  
arm:
	$(RECURSIVE_MAKE)

i386:
	$(RECURSIVE_I386)

install:
	$(RECURSIVE_INSTALL)
  
clean:
	$(RECURSIVE_CLEAN)
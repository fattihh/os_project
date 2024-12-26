#Fatih Uçar  G211210038
#Melih Can Şengün  G211210034	
#Yağmur Kaftar	G211210092
#Eren can Şahin   G211210088
#Bessem El Huseydi  G221210584

HEADERS = program.h
OBJECTS = program.o

default: program

%.o: %.c $(HEADERS)
	gcc -c $< -o $@

program: $(OBJECTS)
	gcc $(OBJECTS) -o $@

clean:
	-rm -f $(OBJECTS)
	-rm -f program

run: program
	./program
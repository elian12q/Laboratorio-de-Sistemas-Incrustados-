all: restart elianyoelvaleria

flag:
	gcc pocs/logica_bandera.c -o test.exe
	./test.exe
	./test.exe --testingInputMP4

compile:
	gcc gstreamer_video_transmitter.c -o transmitter.exe `pkg-config --cflags --libs gstreamer-1.0`

videosrc: restart compile
	./transmitter.exe --testingInputMP4

camerasrc: restart compile
	./transmitter.exe


elianyoelvaleria: compile
	./transmitter.exe

clean:
	rm *.exe

restart:
	sudo service nvargus-daemon restart

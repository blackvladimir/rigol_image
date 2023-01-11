# rigol_image
cmd utility to get data from Rigol osciloscope

tested on Rigol DS1104

#build
`make`

#run
`rigol_image /dev/usbtmc0 output.png` - get image from display
`rigol_pulse /dev/usbtmc0 output.png` - get trace data and export it as [sigrok](https://sigrok.org) session (can be openned in pulseview)

/*{
  "ISFVSN": "2.0",
  "DESCRIPTION": "Blend from one image to another.",
  "CATEGORIES": ["Blend", "Demo"],
  "INPUTS": [
    {"NAME": "startImage", "TYPE": "image"},
    {"NAME": "endImage", "TYPE": "image"},
    {"NAME": "progress", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0, "MAX": 1}
  ]
}*/
void main() {
    gl_FragColor = mix(IMG_NORM_THIS_PIXEL(startImage), IMG_NORM_THIS_PIXEL(endImage), progress);
}

/*{
  "ISFVSN": "2.0",
  "DESCRIPTION": "Invert the colors of an image.",
  "CATEGORIES": ["Color", "Demo"],
  "INPUTS": [
    {"NAME": "inputImage", "TYPE": "image"},
    {"NAME": "amount", "TYPE": "float", "DEFAULT": 1}
  ]
}*/
void main() {
    vec4 color = IMG_THIS_PIXEL(inputImage);
    gl_FragColor = vec4(mix(color.rgb, 1.0 - color.rgb, amount), color.a);
}

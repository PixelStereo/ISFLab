/*{
  "ISFVSN": "2.0",
  "DESCRIPTION": "An animated color gradient.",
  "CATEGORIES": ["Color", "Demo"],
  "INPUTS": [
    {"NAME": "speed", "TYPE": "float", "MIN": 0, "MAX": 4, "DEFAULT": 1}
  ]
}*/
void main() {
    vec2 uv = isf_FragNormCoord;
    gl_FragColor = vec4(uv, 0.5 + 0.5 * sin(TIME * speed + uv.x * 6.28), 1.0);
}

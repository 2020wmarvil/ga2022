#version 450

layout (location = 0) out vec4 outFragColor;

void main()
{
  vec3 color = vec3(1.0, 1.0, 1.0);
  outFragColor = vec4(color, 1.0);
}
#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;

uniform vec3 viewPos;
uniform vec3 objectColor;

void main()
{
    // Headlight effect: Light always comes from the camera
    vec3 lightDir = normalize(viewPos - FragPos);
    vec3 norm = normalize(Normal);
    
    // Two-sided lighting (illuminates inside faces if camera goes inside)
    float diff = max(dot(norm, lightDir), dot(-norm, lightDir));
    
    // High ambient boost (0.4) so the light grey is highly visible
    float diffuse = max(diff, 0.4); 
    
    FragColor = vec4(objectColor * diffuse, 1.0);
}
#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;

// 材质属性
struct Material {
    sampler2D diffuse;  // 漫反射贴图
    sampler2D specular; // 镜面贴图
    float shininess;
}; 

// 光源属性
struct Light {
    vec3 position;
    
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

uniform vec3 viewPos;       // 摄像机/观察者位置
uniform Material material;  // 材质 uniform
uniform Light light;        // 光源 uniform

void main()
{
    // 1. 环境光 (Ambient)
    // 从漫反射贴图中采样材质的环境光颜色
    vec3 ambient = light.ambient * texture(material.diffuse, TexCoords).rgb;
  	
    // 2. 漫反射 (Diffuse)
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(light.position - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    // 从漫反射贴图中采样材质的漫反射颜色
    vec3 diffuse = light.diffuse * diff * texture(material.diffuse, TexCoords).rgb;
    
    // 3. 镜面光 (Specular)
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);  
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    // 从镜面贴图中采样材质的镜面颜色
    vec3 specular = light.specular * spec * texture(material.specular, TexCoords).rgb;
        
    vec3 result = ambient + diffuse + specular;
    FragColor = vec4(result, 1.0);
}
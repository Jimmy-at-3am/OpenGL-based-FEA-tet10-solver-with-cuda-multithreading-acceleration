#ifndef CAMERA_H
#define CAMERA_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Default camera values
const float YAW         = -90.0f;
const float PITCH       =  0.0f;
const float SENSITIVITY =  0.1f;
const float ZOOM        =  45.0f;

// An abstract camera class that processes input and calculates the corresponding Euler Angles, Vectors and Matrices for use in OpenGL
class Camera
{
public:
    // camera Attributes
    glm::vec3 Position;
    glm::vec3 Front;
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;
    // euler Angles
    float Yaw;
    float Pitch;
    // camera options
    float MouseSensitivity;
    float Zoom;

    // CAD Mode Variables
    glm::vec3 OrbitTarget;
    float OrbitRadius;

    // constructor with vectors
    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f), float yaw = YAW, float pitch = PITCH) : Front(glm::vec3(0.0f, 0.0f, -1.0f)), MouseSensitivity(SENSITIVITY), Zoom(ZOOM)
    {
        Position = position;
        WorldUp = up;
        Yaw = yaw;
        Pitch = pitch;
        OrbitTarget = glm::vec3(0.0f, 0.0f, 0.0f);
        OrbitRadius = 5.0f;
        updateCameraVectors();
    }

    // constructor with scalar values
    Camera(float posX, float posY, float posZ, float upX, float upY, float upZ, float yaw, float pitch) : Front(glm::vec3(0.0f, 0.0f, -1.0f)), MouseSensitivity(SENSITIVITY), Zoom(ZOOM)
    {
        Position = glm::vec3(posX, posY, posZ);
        WorldUp = glm::vec3(upX, upY, upZ);
        Yaw = yaw;
        Pitch = pitch;
        OrbitTarget = glm::vec3(0.0f, 0.0f, 0.0f);
        OrbitRadius = 5.0f;
        updateCameraVectors();
    }

    // returns the view matrix calculated using Euler Angles and the LookAt Matrix
    glm::mat4 GetViewMatrix()
    {
        return glm::lookAt(Position, Position + Front, Up);
    }

    void UpdatePosition() {
        Position = OrbitTarget - Front * OrbitRadius;
    }

    // CAD Right-Click Orbit
    void ProcessMouseOrbit(float xoffset, float yoffset, GLboolean constrainPitch = true)
    {
        float cadSens = MouseSensitivity * 3.0f; 

        Yaw += xoffset * cadSens;
        Pitch += yoffset * cadSens;

        if (constrainPitch)
        {
            if (Pitch > 89.0f) Pitch = 89.0f;
            if (Pitch < -89.0f) Pitch = -89.0f;
        }

        updateCameraVectors();
        UpdatePosition();
    }

    // CAD Middle-Click Pan
    void ProcessMousePan(float xoffset, float yoffset)
    {
        float fovRad = glm::radians(Zoom);
        float panSpeed = (2.0f * OrbitRadius * tan(fovRad * 0.5f)) / 800.0f;
        
        glm::vec3 panDelta = Right * (xoffset * panSpeed) + Up * (yoffset * panSpeed);
        
        OrbitTarget -= panDelta;
        UpdatePosition();
    }

    // processes input received from a mouse scroll-wheel event.
    void ProcessMouseScroll(float yoffset)
    {
        OrbitRadius -= yoffset * (OrbitRadius * 0.15f);
        if (OrbitRadius < 0.5f) OrbitRadius = 0.5f;
        UpdatePosition();
    }

private:
    // calculates the front vector from the Camera's (updated) Euler Angles
    void updateCameraVectors()
    {
        // calculate the new Front vector
        glm::vec3 front;
        front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        front.y = sin(glm::radians(Pitch));
        front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        Front = glm::normalize(front);
        // also re-calculate the Right and Up vector
        Right = glm::normalize(glm::cross(Front, WorldUp));  
        Up    = glm::normalize(glm::cross(Right, Front));
    }
};
#endif
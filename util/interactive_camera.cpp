// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "interactive_camera.h"
#include <cmath>
#include <iostream>
#include <glm/ext.hpp>
#include <glm/gtx/transform.hpp>

const glm::mat4 &Camera::transform() const
{
    return camera;
}
const glm::mat4 &Camera::inv_transform() const
{
    return inv_camera;
}
glm::vec3 Camera::eye() const
{
    return glm::vec3{inv_camera * glm::vec4{0, 0, 0, 1}};
}
glm::vec3 Camera::dir() const
{
    return glm::normalize(glm::vec3{inv_camera * glm::vec4{0, 0, -1, 0}});
}
glm::vec3 Camera::up() const
{
    return glm::normalize(glm::vec3{inv_camera * glm::vec4{0, 1, 0, 0}});
}

// Project the point in [-1, 1] screen space onto the arcball sphere
static glm::quat screen_to_arcball(const glm::vec2 &p);

ArcballCamera::ArcballCamera(const glm::vec3 &eye, const glm::vec3 &center, const glm::vec3 &up)
{
    const glm::vec3 dir = center - eye;
    glm::vec3 z_axis = glm::normalize(dir);
    glm::vec3 x_axis = glm::normalize(glm::cross(z_axis, glm::normalize(up)));
    glm::vec3 y_axis = glm::normalize(glm::cross(x_axis, z_axis));
    x_axis = glm::normalize(glm::cross(z_axis, y_axis));

    center_translation = glm::inverse(glm::translate(center));
    translation = glm::translate(glm::vec3(0.f, 0.f, -glm::length(dir)));
    rotation = glm::normalize(glm::quat_cast(glm::transpose(glm::mat3(x_axis, y_axis, -z_axis))));

    update_camera();
}
void ArcballCamera::rotate(glm::vec2 prev_mouse, glm::vec2 cur_mouse)
{
    // Clamp mouse positions to stay in NDC
    cur_mouse = glm::clamp(cur_mouse, glm::vec2{-1, -1}, glm::vec2{1, 1});
    prev_mouse = glm::clamp(prev_mouse, glm::vec2{-1, -1}, glm::vec2{1, 1});

    const glm::quat mouse_cur_ball = screen_to_arcball(cur_mouse);
    const glm::quat mouse_prev_ball = screen_to_arcball(prev_mouse);

    rotation = mouse_cur_ball * mouse_prev_ball * rotation;
    update_camera();
}
void ArcballCamera::pan(glm::vec2 mouse_delta)
{
    const float zoom_amount = std::abs(translation[3][2]);
    glm::vec4 motion(mouse_delta.x * zoom_amount, mouse_delta.y * zoom_amount, 0.f, 0.f);
    // Find the panning amount in the world space
    motion = inv_camera * motion;

    center_translation = glm::translate(glm::vec3(motion)) * center_translation;
    update_camera();
}
void ArcballCamera::zoom(const float zoom_amount)
{
    const glm::vec3 motion(0.f, 0.f, zoom_amount);

    translation = glm::translate(motion) * translation;
    update_camera();
}
glm::vec3 ArcballCamera::center() const
{
    return -glm::column(center_translation, 3);
}
void ArcballCamera::update_camera()
{
    camera = translation * glm::mat4_cast(rotation) * center_translation;
    inv_camera = glm::inverse(camera);
}

glm::quat screen_to_arcball(const glm::vec2 &p)
{
    const float dist = glm::dot(p, p);
    // If we're on/in the sphere return the point on it
    if (dist <= 1.f) {
        return glm::quat(0.0, p.x, p.y, std::sqrt(1.f - dist));
    } else {
        // otherwise we project the point onto the sphere
        const glm::vec2 proj = glm::normalize(p);
        return glm::quat(0.0, proj.x, proj.y, 0.f);
    }
}

OrientedCamera::OrientedCamera(glm::vec3 up, glm::vec3 eye, glm::quat rotation)
    : global_up(up) {
    inv_camera = glm::translate(eye) * mat4_cast(rotation);
    camera = inverse(inv_camera);
}
void OrientedCamera::rotate(glm::vec2 prev_mouse, glm::vec2 cur_mouse) {
    glm::vec2 mouse_delta = cur_mouse - prev_mouse;
    camera = glm::rotate(mouse_delta.y * sensitivity, glm::vec3(-1, 0, 0)) * camera;
    glm::vec3 local_up = normalize(glm::vec3{camera * glm::vec4{global_up.x, global_up.y, global_up.z, 0}});
    camera = glm::rotate(mouse_delta.x * sensitivity, local_up) * camera;
    update_camera();
}
void OrientedCamera::pan(glm::vec2 mouse_delta) {
    move_local(glm::vec3(mouse_delta.x, mouse_delta.y, 0), length(eye()));
    update_camera();
}
void OrientedCamera::zoom(float zoom_amount) {
//    move_local(glm::vec3(0, 0, 1), zoom_amount);
    speed *= std::exp(zoom_amount);
}
void OrientedCamera::move_local(glm::vec3 local_dir, float speed) {
    camera = glm::translate(local_dir * -(this->speed * speed)) * camera;
    update_camera();
}
void OrientedCamera::update_camera() {
    // todo: lock to up axis?
    inv_camera = inverse(camera);
}
glm::vec3 OrientedCamera::center() const {
    return eye() + dir();
}

void OrientedCamera::set_position(glm::vec3 pos) {
    inv_camera[3] = glm::vec4(pos, 1.0f);
    camera = inverse(inv_camera);
}
void OrientedCamera::set_direction(glm::vec3 dir) {
    dir = normalize(dir);
    glm::vec3 prev_right = cross(this->up(), -this->dir());
    glm::vec3 right = cross(global_up, -dir);
    float sin_theta = glm::length(right);
    if (sin_theta < 0.2f) { // ~12 deg off global up
        if (sin_theta < 0.001f) // ~0.05 deg off global up
            right = prev_right;
        else if (dot(right, prev_right) < 0.0f)
            right = -right;
    }
    glm::vec3 up = cross(-dir, right);
    right = normalize(right);
    up = normalize(up);
    inv_camera[0] = glm::vec4(right, 0.0f);
    inv_camera[1] = glm::vec4(up, 0.0f);
    inv_camera[2] = glm::vec4(-dir, 0.0f);
    camera = inverse(inv_camera);
}
void OrientedCamera::set_direction(glm::vec3 dir, glm::vec3 up) {
    dir = normalize(dir);
    glm::vec3 right = normalize(cross(up, -dir));
    up = normalize(cross(-dir, right));
    inv_camera[0] = glm::vec4(right, 0.0f);
    inv_camera[1] = glm::vec4(up, 0.0f);
    inv_camera[2] = glm::vec4(-dir, 0.0f);
    camera = inverse(inv_camera);
}

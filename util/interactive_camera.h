// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <glm/glm.hpp>
#include <glm/ext.hpp>

struct Camera {
	// camera is the full camera transform,
	// inv_camera is stored as well to easily compute
	// eye position and world space rotation axes
	glm::mat4 camera, inv_camera;

	// Get the camera transformation matrix
	const glm::mat4& transform() const;
	// Get the camera's inverse transformation matrix
	const glm::mat4& inv_transform() const;
	// Get the eye position of the camera in world space
	glm::vec3 eye() const;
	// Get the eye direction of the camera in world space
	glm::vec3 dir() const;
	// Get the up direction of the camera in world space
	glm::vec3 up() const;
};

/* A simple arcball camera that moves around the camera's focal point.
 * The mouse inputs to the camera should be in normalized device coordinates,
 * where the top-left of the screen corresponds to [-1, 1], and the bottom
 * right is [1, -1].
 */
class ArcballCamera : public Camera {
	// We store the unmodified look at matrix along with
	// decomposed translation and rotation components
	glm::mat4 center_translation, translation;
	glm::quat rotation;

public:
	/* Create an arcball camera focused on some center point
	 * screen: [win_width, win_height]
	 */
	ArcballCamera(const glm::vec3 &eye, const glm::vec3 &center,
		   const glm::vec3 &up);
	/* Rotate the camera from the previous mouse position to the current
	 * one. Mouse positions should be in normalized device coordinates
	 */
	void rotate(glm::vec2 prev_mouse, glm::vec2 cur_mouse);
	/* Pan the camera given the translation vector. Mouse
	 * delta amount should be in normalized device coordinates
	 */
	void pan(glm::vec2 mouse_delta);
	/* Zoom the camera given the zoom amount to (i.e., the scroll amount).
	 * Positive values zoom in, negative will zoom out.
	 */
	void zoom(const float zoom_amount);
	// Get the center of rotation of the camera in world space
	glm::vec3 center() const;

private:
	void update_camera();
};

/* A simple oriented camera aligned with an up vector.
 */
struct OrientedCamera : public Camera {
	glm::vec3 global_up;
	float sensitivity = 1;
	float speed = 1;

	OrientedCamera(glm::vec3 up, glm::vec3 eye = glm::vec3(0, 0, 0), glm::quat rotation = glm::quat(1, 0, 0, 0));
	/* Rotate the camera from the previous mouse position to the current
	 * one. Mouse positions should be in normalized device coordinates
	 */
	void rotate(glm::vec2 prev_mouse, glm::vec2 cur_mouse);
	/* Pan the camera given the translation vector. Mouse
	 * delta amount should be in normalized device coordinates
	 */
	void pan(glm::vec2 mouse_delta);
	/* Zoom the camera given the zoom amount to (i.e., the scroll amount).
	 * Positive values zoom in, negative will zoom out.
	 */
	void zoom(float zoom_amount);
	/* Move the camera along a local vector.
	 */
	void move_local(glm::vec3 local_dir, float speed);
	// Get the center of rotation of the camera in world space
	glm::vec3 center() const;

	void set_position(glm::vec3 pos);
	void set_direction(glm::vec3 dir);
	void set_direction(glm::vec3 dir, glm::vec3 up);

private:
	void update_camera();
};

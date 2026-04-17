#define GLM_ENABLE_EXPERIMENTAL

#define NANOSVG_IMPLEMENTATION	// Expands implementation
#include "../3rdparty/nanosvg/src/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "../3rdparty/nanosvg/src/nanosvgrast.h"

#include <GL\glew.h>
#include <GLFW\glfw3.h>
#include <string>
#include <iostream>
#include "./common/debugging.h"
#include "./common/renderable.h"
#include "./common/shaders.h"
#include "./common/simple_shapes.h"
#include "./common/carousel/carousel.h"
#include "./common/carousel/carousel_to_renderable.h"
#include "./common/carousel/carousel_loader.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "./common/gltf_loader.h"
#include "./common/texture.h"

#include <algorithm>
#include <conio.h>
#include <direct.h>
#include "./common/matrix_stack.h"
#include "./common/intersection.h"
#include "./common/trackball.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <chrono>

struct CPUTimer {
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
    float elapsed_ms = 0.0f;

    void start() {
        start_time = std::chrono::high_resolution_clock::now();
    }

    void stop() {
        auto end_time = std::chrono::high_resolution_clock::now();
        elapsed_ms = std::chrono::duration<float, std::milli>(end_time - start_time).count();
    }
};

struct GPUTimer {
    unsigned int queryID;
    float elapsed_ms = 0.0f;

    void init() {
        glGenQueries(1, &queryID);
    }

    void start() {
        // Diciamo a OpenGL di iniziare a contare il tempo
        glBeginQuery(GL_TIME_ELAPSED, queryID);
    }

    void stop() {
        // Diciamo a OpenGL di smettere di contare
        glEndQuery(GL_TIME_ELAPSED);
    }

    void update_results() {
        GLuint64 elapsed_ns;
        // Recuperiamo il risultato in nanosecondi
        glGetQueryObjectui64v(queryID, GL_QUERY_RESULT, &elapsed_ns);
        // Convertiamo in millisecondi per comodità di lettura
        elapsed_ms = static_cast<float>(elapsed_ns) / 1000000.0f;
    }
};

trackball tb[2];
int curr_tb;

/* projection matrix*/
glm::mat4 proj;

/* view matrix */
glm::mat4 view;

matrix_stack stack;
float scaling_factor = 1.0;
double lastX = 400, lastY = 400;
bool firstMouse = true;

GLuint dummyWhiteTex, dummyBlackTex, dummyNormalTex;

race r;

struct Camera {
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::vec3 worldUp;
    float yaw;
    float pitch;
    float speed;
    float sensitivity;
    float Zoom = 45.f;
	bool cameraman_view = false;
	unsigned int cameraman_idx = 0;

    Camera() : position(0.f, 1.f, 1.5f), worldUp(0.f, 1.f, 0.f), yaw(-90.f), pitch(0.f), speed(0.1f), sensitivity(0.1f) {
        update_camera_vectors();
    }

    void move_forward() {
        glm::vec3 newPos = position + speed * front;
        if (newPos.y > 0.1f)  // blocca la camera se va troppo in basso
            position = newPos;
    }

    void move_backward() { position -= speed * front; }
    void move_left() { position -= glm::normalize(glm::cross(front, up)) * speed; }
    void move_right() { position += glm::normalize(glm::cross(front, up)) * speed; }

    void process_mouse_movement(float xoffset, float yoffset) {
        xoffset *= sensitivity;
        yoffset *= sensitivity;

        yaw += xoffset;
        pitch += yoffset;

        if (pitch > 89.0f)  pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;

        update_camera_vectors();
    }

    glm::mat4 get_view_matrix(){

        if (cameraman_view) {
            if (cameraman_idx >= r.cameramen().size()) {
				cameraman_idx = 0;
            }
            float s = 1.f / r.bbox().diagonal();
            glm::vec3 center = r.bbox().center();
            cameraman c = r.cameramen()[cameraman_idx];
            glm::mat4 frame = c.frame;
            frame = glm::translate(frame, glm::vec3(0.0f, 5.f, 17.f));
            frame = glm::translate(glm::mat4(1), -center) * frame;
            frame[3] = glm::scale(glm::mat4(1), glm::vec3(s)) * frame[3] + glm::vec4(0, 0.005, 0, 0);
            return glm::inverse(frame);
		}
        return glm::lookAt(position, position + front, up);
    }

    void ProcessMouseScroll(float yoffset)
    {
        Zoom -= (float)yoffset;
        if (Zoom < 1.0f)
            Zoom = 1.0f;
        if (Zoom > 45.0f)
            Zoom = 45.0f;
    }

    glm::vec3 get_position() {
        if (cameraman_view && r.cameramen().size() > 0) {
            cameraman c = r.cameramen()[cameraman_idx];
            return glm::vec3(c.frame[3]);
        }
        return position;
    }

private:
    void update_camera_vectors() {
        glm::vec3 f;
        f.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        f.y = sin(glm::radians(pitch));
        f.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        front = glm::normalize(f);
        right = glm::normalize(glm::cross(front, worldUp));
        up = glm::normalize(glm::cross(right, front));
    }
};

Camera camera;

/* callback function called when the mouse is moving */
static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos)
{
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }
    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;
    lastX = xpos;
    lastY = ypos;

    camera.process_mouse_movement(xoffset, yoffset);
}

/* callback function called when a mouse button is pressed */
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        //double xpos, ypos;
        //tb[curr_tb].mouse_press(proj, view, xpos, ypos);
        glfwSetCursorPosCallback(window, cursor_position_callback);
        firstMouse = true;

    }
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
        tb[curr_tb].mouse_release();
        glfwSetCursorPosCallback(window, NULL);
    }
}

/* callback function called when a mouse wheel is rotated */
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    if (!camera.cameraman_view) {
        if (curr_tb == 0)
            tb[0].mouse_scroll(xoffset, yoffset);
        camera.ProcessMouseScroll(static_cast<float>(yoffset));
    }

}


void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        switch (key) {
        case GLFW_KEY_W: camera.move_forward();  break;
        case GLFW_KEY_S: camera.move_backward(); break;
        case GLFW_KEY_A: camera.move_left();     break;
        case GLFW_KEY_D: camera.move_right();    break;
        case GLFW_KEY_T: curr_tb = 1 - curr_tb;   break;
        case GLFW_KEY_R: camera.cameraman_view = false; camera = Camera(); break;
        case GLFW_KEY_C:
            if (!camera.cameraman_view) {
                
                camera.cameraman_view = true;
				tb[0].reset();
            }
            else {
                // Ero già in modalità cameraman: passo al prossimo
                camera.cameraman_idx += 1;
            }
            break;
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(window, true); break;
        }
    }
}

GLuint createDummyTexture(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);

    unsigned char pixelData[] = { r, g, b, a };

    // Disabilita l'allineamento dei byte per evitare crash con texture minuscole
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Crea la texture 1x1
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixelData);

    // Parametri base (essendo 1x1, GL_NEAREST è perfetto)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    return texID;
}

// Chiamala all'avvio dell'applicazione
void initDummyTextures(GLuint& dummyWhiteTex, GLuint& dummyBlackTex, GLuint& dummyNormalTex) {
    dummyWhiteTex = createDummyTexture(255, 255, 255, 255);
    dummyBlackTex = createDummyTexture(0, 0, 0, 255);
    dummyNormalTex = createDummyTexture(128, 128, 255, 255);
}

// Funzione per calcolare il colore della luce in base alla direzione
glm::vec3 get_sun_color(const glm::vec3& sun_dir) {
    // Invertiamo la Y così l'elevazione è: 1.0 (Mezzogiorno), 0.0 (Orizzonte), -1.0 (Notte)
    float elevation = sun_dir.y;

    // Definisci la tua palette di colori (HDR, quindi possono superare 1.0)
    glm::vec3 color_noon = glm::vec3(4.0f, 4.0f, 3.8f);   // Bianco leggermente caldo
    glm::vec3 color_sunset = glm::vec3(4.0f, 1.5f, 0.2f);   // Arancio/Rosso intenso del tramonto
    glm::vec3 color_night = glm::vec3(0.05f, 0.08f, 0.2f); // Luce lunare bluastra molto debole

    if (elevation > 0.1f) {
        // --- GIORNO (Da mezzogiorno a poco prima del tramonto) ---
        // Ricalcoliamo il range [0.1, 1.0] su una scala [0.0, 1.0] per il mix
        float t = (elevation - 0.1f) / 0.9f;
        // smoothstep rende la transizione dei colori più morbida e naturale
        t = glm::smoothstep(0.0f, 1.0f, t);
        return glm::mix(color_sunset, color_noon, t);

    }
    else if (elevation > -0.1f) {
        // --- CREPUSCOLO (Il sole sta attraversando la linea dell'orizzonte) ---
        // Range [-0.1, 0.1] mappato su [0.0, 1.0]
        float t = (elevation + 0.1f) / 0.2f;
        t = glm::smoothstep(0.0f, 1.0f, t);
        return glm::mix(color_night, color_sunset, t);

    }
    else {
        // --- NOTTE FONDA ---
        return color_night;
    }
}

void render_terrain(renderable& r_terrain, matrix_stack& stack, shader& shader, GLuint grass_texture_id) {
    check_gl_errors(__LINE__, __FILE__);
	glUniform1i(shader["alpha_mode"], 3); // nessuna trasparenza

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, grass_texture_id);
    glUniform1i(shader["uTex"], 0);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, dummyWhiteTex);
	glUniform1i(shader["uMRTex"], 1);
	glUniform1f(shader["uMetallic"], 0.0);
    glUniform1f(shader["uRoughness"], 0.98f);

    glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, dummyNormalTex);
    glUniform1i(shader["uNormalTex"], 2);

	glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, dummyBlackTex);
	glUniform1i(shader["uEmissive"], 3);

	glActiveTexture(GL_TEXTURE4);
	glBindTexture(GL_TEXTURE_2D, dummyWhiteTex);
	glUniform1i(shader["uOcclusion"], 4);
	glUniform1f(shader["uOcclusionStrength"], 1.0);

	glUniform1i(shader["isTerrain"], 1);
    glDepthRange(0.01, 1);
    glUniformMatrix4fv(shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);
    r_terrain.bind();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDrawElements(GL_TRIANGLES, r_terrain().count, GL_UNSIGNED_INT, 0);
}

void render_track(renderable& r_track, shader& shader, GLuint road_texture_id, GLuint road_normal_id, race& r) {

	glUniform1i(shader["alpha_mode"], 3); 

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, road_texture_id); 
    glUniform1i(shader["uTex"], 0); 

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, dummyWhiteTex);
    glUniform1i(shader["uMRTex"], 1);
    glUniform1f(shader["uMetallic"], 0.0);
    glUniform1f(shader["uRoughness"], 0.6f);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, road_normal_id);
    glUniform1i(shader["uNormalTex"], 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, dummyBlackTex);
    glUniform1i(shader["uEmissive"], 3);

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, dummyWhiteTex);
    glUniform1i(shader["uOcclusion"], 4);
    glUniform1f(shader["uOcclusionStrength"], 1.0);

	glUniform1i(shader["isTerrain"], 0);
    
    stack.push();
    stack.mult(tb[0].matrix());

    float s = 1.f / r.bbox().diagonal();
    glm::vec3 c = r.bbox().center();
    stack.mult(glm::scale(glm::mat4(1.f), glm::vec3(s)));
    stack.mult(glm::translate(glm::mat4(1.f), -c));
    glUniformMatrix4fv(shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);
    stack.pop();
    // --- prova per evitare z-fighting con il terreno
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-20.0f, -20.0f);
    r_track.bind();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDrawElements(GL_TRIANGLES, r_track().count, GL_UNSIGNED_INT, 0);

    glDisable(GL_POLYGON_OFFSET_FILL); //fine ossfet
}

void render_tree(race& r, matrix_stack& stack, shader& shader,box3 tree_bbox, std::vector<renderable>& tree_objects ) {
    for (unsigned int i = 0; i < r.trees().size(); ++i) {
        stack.push();

        glm::vec3 curr_pos = r.trees()[i].pos;
        stack.mult(glm::translate(glm::mat4(1.f), curr_pos));
        stack.mult(glm::translate(glm::mat4(1.f), glm::vec3(0.0f, 2.53f, 0.0f)));
        stack.mult(glm::scale(glm::mat4(1.f), glm::vec3(7.3f)));

        float scale = 1.f / tree_bbox.diagonal();
        stack.mult(glm::scale(glm::mat4(1.f), glm::vec3(scale, scale, scale)));
        stack.mult(glm::translate(glm::mat4(1.f), glm::vec3(-tree_bbox.center())));

        for (unsigned int j = 0; j < tree_objects.size(); ++j) {
            tree_objects[j].bind();
            stack.push();

            // each object had its own transformation that was read in the gltf file
            stack.mult(tree_objects[j].transform);

            glUniform1i(shader["alpha_mode"], tree_objects[j].mater.alpha_mode == "MASK" ? 1 : (tree_objects[j].mater.alpha_mode == "BLEND" ? 2 : 0));
			glUniform1f(shader["alpha_cutoff"], tree_objects[j].mater.alpha_cutoff);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tree_objects[j].mater.base_color_texture);
            glUniform1i(shader["uTex"], 0);
            glm::vec4 color = glm::vec4((float)tree_objects[j].mater.base_color_factor[0], (float)tree_objects[j].mater.base_color_factor[1], (float)tree_objects[j].mater.base_color_factor[2], (float)tree_objects[j].mater.base_color_factor[3]);
            glUniform4fv(shader["uColor"], 1, &color[0]);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, tree_objects[j].mater.metallic_roughness_texture);
            glUniform1i(shader["uMRTex"], 1);
			glUniform1f(shader["uMetallic"], tree_objects[j].mater.metallic_factor);
            glUniform1f(shader["uRoughness"], tree_objects[j].mater.roughness_factor);

            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, tree_objects[j].mater.normal_texture);
			glUniform1i(shader["uNormalTex"], 2);

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, tree_objects[j].mater.emissive_texture);
            glUniform1i(shader["uEmissive"], 3);

            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, tree_objects[j].mater.occlusion_texture);
            glUniform1i(shader["uOcclusion"], 4);

            glUniformMatrix4fv(shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);

            glDrawElements(tree_objects[j]().mode, tree_objects[j]().count, tree_objects[j]().itype, 0);
            stack.pop();
        }
        stack.pop();
    }
}

void render_lamps(race& r, matrix_stack& stack, shader& shader, box3 lamp_bbox, std::vector<renderable>& lamp_objects) {
    for (unsigned int i = 0; i < r.lamps().size(); ++i) {
        stack.push();

        glm::vec3 curr_pos = r.lamps()[i].pos;
        stack.mult(glm::translate(glm::mat4(1.f), curr_pos));
        stack.mult(glm::translate(glm::mat4(1.f), glm::vec3(0.0f, 2.53f, -0.5f)));
        if (i < r.lamps().size() / 2) {
            stack.mult(glm::rotate(glm::mat4(1.f), glm::radians(180.f), glm::vec3(0, 1, 0)));
        }
        stack.mult(glm::scale(glm::mat4(1.f), glm::vec3(7.3f)));

        float scale = 1.f / lamp_bbox.diagonal();
        stack.mult(glm::scale(glm::mat4(1.f), glm::vec3(scale, scale, scale)));
        stack.mult(glm::translate(glm::mat4(1.f), glm::vec3(-lamp_bbox.center())));

        for (unsigned int j = 0; j < lamp_objects.size(); ++j) {
            lamp_objects[j].bind();
            stack.push();

            // each object had its own transformation that was read in the gltf file
            stack.mult(lamp_objects[j].transform);

            glUniform1i(shader["alpha_mode"], lamp_objects[j].mater.alpha_mode == "MASK" ? 1 : (lamp_objects[j].mater.alpha_mode == "BLEND" ? 2 : 0));
            glUniform1f(shader["alpha_cutoff"], lamp_objects[j].mater.alpha_cutoff);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, lamp_objects[j].mater.base_color_texture);
            glUniform1i(shader["uTex"], 0);
            glm::vec4 color = glm::vec4((float)lamp_objects[j].mater.base_color_factor[0], (float)lamp_objects[j].mater.base_color_factor[1], (float)lamp_objects[j].mater.base_color_factor[2], (float)lamp_objects[j].mater.base_color_factor[3]);
            glUniform4fv(shader["uColor"], 1, &color[0]);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, lamp_objects[j].mater.metallic_roughness_texture);
            glUniform1i(shader["uMRTex"], 1);
            glUniform1f(shader["uMetallic"], lamp_objects[j].mater.metallic_factor);
            glUniform1f(shader["uRoughness"], lamp_objects[j].mater.roughness_factor);

            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, lamp_objects[j].mater.normal_texture);
            glUniform1i(shader["uNormalTex"], 2);

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, lamp_objects[j].mater.emissive_texture);
            glUniform1i(shader["uEmissive"], 3);

            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, lamp_objects[j].mater.occlusion_texture);
            glUniform1i(shader["uOcclusion"], 4);
            glUniformMatrix4fv(shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);

            glDrawElements(lamp_objects[j]().mode, lamp_objects[j]().count, lamp_objects[j]().itype, 0);
            stack.pop();
        }
        stack.pop();
    }
}

void render_cameramen(race& r, matrix_stack& stack, shader& shader, box3 camera_bbox, std::vector<renderable>& camera_objects) {
    for (unsigned int ic = 0; ic < r.cameramen().size(); ++ic) {
        if (camera.cameraman_view && ic == camera.cameraman_idx) {
            continue;
        }
        stack.push();
        stack.mult(r.cameramen()[ic].frame);
        float scale = 1.f / camera_bbox.diagonal();
        stack.mult(glm::scale(glm::mat4(1.f), glm::vec3(scale*2.2f)));
        stack.mult(glm::rotate(glm::mat4(1.f), glm::radians(90.f), glm::vec3(0, 1, 0)));
        stack.mult(glm::translate(glm::mat4(1.f), glm::vec3(-camera_bbox.center().x, -camera_bbox.min.y, -camera_bbox.center().z)));

        for (unsigned int j = 0; j < camera_objects.size(); ++j) {
            camera_objects[j].bind();
            stack.push();
            // each object had its own transformation that was read in the gltf file
            stack.mult(camera_objects[j].transform);

            glUniform1i(shader["alpha_mode"], camera_objects[j].mater.alpha_mode == "MASK" ? 1 : (camera_objects[j].mater.alpha_mode == "BLEND" ? 2 : 0));
            glUniform1f(shader["alpha_cutoff"], camera_objects[j].mater.alpha_cutoff);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, camera_objects[j].mater.base_color_texture);
            glUniform1i(shader["uTex"], 0);
            glm::vec4 color = glm::vec4((float)camera_objects[j].mater.base_color_factor[0], (float)camera_objects[j].mater.base_color_factor[1], (float)camera_objects[j].mater.base_color_factor[2], (float)camera_objects[j].mater.base_color_factor[3]);
            glUniform4fv(shader["uColor"], 1, &color[0]);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, camera_objects[j].mater.metallic_roughness_texture);
            glUniform1i(shader["uMRTex"], 1);
            glUniform1f(shader["uMetallic"], camera_objects[j].mater.metallic_factor);
            glUniform1f(shader["uRoughness"], camera_objects[j].mater.roughness_factor);

            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, camera_objects[j].mater.normal_texture);
            glUniform1i(shader["uNormalTex"], 2);

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, camera_objects[j].mater.emissive_texture);
            glUniform1i(shader["uEmissive"], 3);

            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, camera_objects[j].mater.occlusion_texture);
            glUniform1i(shader["uOcclusion"], 4);

            glUniformMatrix4fv(shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);

            glDrawElements(camera_objects[j]().mode, camera_objects[j]().count, camera_objects[j]().itype, 0);
            stack.pop();
        }
        stack.pop();
    }
}

void render_cars(race& r, matrix_stack& stack, shader& shader, box3 car_bbox, std::vector<renderable>& car_objects) {
    terrain terrain = r.ter();
    for (unsigned int ic = 0; ic < r.cars().size(); ++ic) {
        stack.push();
        stack.mult(r.cars()[ic].frame);
        glm::vec3 car_pos = glm::vec3(r.cars()[ic].frame[3].x, r.cars()[ic].frame[3].y, r.cars()[ic].frame[3].z);
        float car_height = car_pos.y;
        float scale = 1.f / car_bbox.diagonal();
        stack.mult(glm::scale(glm::mat4(1.f), glm::vec3(7.3f*scale)));

        stack.mult(glm::rotate(glm::mat4(1.f), glm::radians(180.f), glm::vec3(0, 1, 0)));

        float bottom_y = car_bbox.min.y;
        stack.mult(glm::translate(glm::mat4(1.f), glm::vec3(-car_bbox.center().x, -car_bbox.min.y+2.0f, -car_bbox.center().z)));

        for (unsigned int j = 0; j < car_objects.size(); j++) {
            car_objects[j].bind();
            stack.push();
            // each object had its own transformation that was read in the gltf file
            stack.mult(car_objects[j].transform);

            glUniform1i(shader["alpha_mode"], car_objects[j].mater.alpha_mode == "MASK" ? 1 : (car_objects[j].mater.alpha_mode == "BLEND" ? 2 : 0));
            glUniform1f(shader["alpha_cutoff"], car_objects[j].mater.alpha_cutoff);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, car_objects[j].mater.base_color_texture);
            glUniform1i(shader["uTex"], 0);
            glm::vec4 color = glm::vec4((float)car_objects[j].mater.base_color_factor[0], (float)car_objects[j].mater.base_color_factor[1], (float)car_objects[j].mater.base_color_factor[2], (float)car_objects[j].mater.base_color_factor[3]);
            glUniform4fv(shader["uColor"], 1, &color[0]);

            glUniform1f(shader["uMetallic"], car_objects[j].mater.metallic_factor);
            glUniform1f(shader["uRoughness"], car_objects[j].mater.roughness_factor);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, car_objects[j].mater.metallic_roughness_texture);
            glUniform1i(shader["uMRTex"], 1);

            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, car_objects[j].mater.normal_texture);
            glUniform1i(shader["uNormalTex"], 2);

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, car_objects[j].mater.emissive_texture);
            glUniform1i(shader["uEmissive"], 3);

            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, car_objects[j].mater.occlusion_texture);
            glUniform1i(shader["uOcclusion"], 4);
			glUniform1f(shader["uOcclusionStrength"], car_objects[j].mater.occlusion_strength);

            glUniformMatrix4fv(shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);
            glDrawElements(car_objects[j]().mode, car_objects[j]().count, car_objects[j]().itype, 0);
            stack.pop();
        }
        stack.pop();
    }
}


int main(int argc, char** argv)
{
    carousel_loader::load("./assets/small_test.svg", "./assets/terrain_256.png", r);
    for (int i = 0; i < 10; ++i)
        r.add_car();

    GLFWwindow* window;
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_SAMPLES, 4);
    window = glfwCreateWindow(800, 800, "CarOusel", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    //glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

    //glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwMakeContextCurrent(window);
    glewInit();
    glEnable(GL_MULTISAMPLE);
    printout_opengl_glsl_info();

    /* initialize IMGUI */
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init();
    /* end IMGUI initialization */

    renderable fram = shape_maker::frame();
    renderable r_cube = shape_maker::cube();
    renderable r_track; r_track.create(); game_to_renderable::to_track(r, r_track);
    renderable r_terrain; r_terrain.create(); game_to_renderable::to_heightfield(r, r_terrain); // 5.0f = numero ripetizioni della texture
	renderable r_sun_dir = shape_maker::cylinder(6, 0.05f);
    std::cout << "VAO: " << r_terrain.vao << std::endl;
    std::cout << "Index buffer (EBO): " << r_terrain().ind << std::endl;
    std::cout << "Index count: " << r_terrain().count << std::endl;

	initDummyTextures(dummyWhiteTex, dummyBlackTex, dummyNormalTex);

    gltf_loader gltfL_camera;
    gltfL_camera.assignDummyTexture(&dummyWhiteTex, &dummyBlackTex, &dummyNormalTex);
    std::vector<renderable> camera_objects;
    box3 camera_bbox;
    gltfL_camera.load_to_renderable("assets/modelli/camera.glb", camera_objects, camera_bbox);
    gltf_loader gltfL_car;
    gltfL_car.assignDummyTexture(&dummyWhiteTex, &dummyBlackTex, &dummyNormalTex);
    std::vector<renderable> car_objects;
    box3 car_bbox;
    gltfL_car.load_to_renderable("assets/modelli/car_low-poly.glb", car_objects, car_bbox);
    gltf_loader gltfL_tree;
    gltfL_tree.assignDummyTexture(&dummyWhiteTex, &dummyBlackTex, &dummyNormalTex);
    std::vector<renderable> tree_objects;
    box3 tree_bbox;
    gltfL_tree.load_to_renderable("assets/modelli/albero.glb", tree_objects, tree_bbox);
    //renderable r_trees; r_trees.create(); game_to_renderable::to_tree(r, r_trees);
    gltf_loader gltfL_lamp;
	gltfL_lamp.assignDummyTexture(&dummyWhiteTex, &dummyBlackTex, &dummyNormalTex);
    std::vector<renderable> lamp_objects;
    box3 lamp_bbox;
    gltfL_lamp.load_to_renderable("assets/modelli/low_poly_lamp.glb", lamp_objects, lamp_bbox);
    //renderable r_lamps; r_lamps.create(); game_to_renderable::to_lamps(r, r_lamps);

    shader basic_shader;
    basic_shader.create_program("shaders/basic.vert", "shaders/basic.frag");

    shader model_shader;
    model_shader.create_program("shaders/light.vert", "shaders/light.frag");

	shader current_program;

    // --- Caricamento texture tileabile per l'erba ---
	texture grass_texture;
	grass_texture.load("./assets/texture/grass_tile.PNG", 0);
    // --- Caricamento texture della  pista ---
	texture road_texture;
	road_texture.load("./assets/texture/street_tile2.PNG", 0);

	texture road_normal_tex;
	road_normal_tex.load("./assets/texture/normal_map.JPG", 1);

    glViewport(0, 0, 800, 800);

    tb[0].reset();
    tb[0].set_center_radius(glm::vec3(0, 0, 0), 1.f);
    curr_tb = 0;

    proj = glm::perspective(glm::radians(45.f), 1.f, 0.1f, 100.f); //modificato
    check_gl_errors(__LINE__, __FILE__);
	glUseProgram(model_shader.program);
    glUniformMatrix4fv(model_shader["uProj"], 1, GL_FALSE, &proj[0][0]);
	current_program = model_shader;

    //glUseProgram(basic_shader.program);
    //glUniformMatrix4fv(basic_shader["uProj"], 1, GL_FALSE, &proj[0][0]);

    r.start(11, 0, 0, 600);
    r.update();

    matrix_stack stack;
    glEnable(GL_DEPTH_TEST);

    // inizializzazione per misura delle risorse
    CPUTimer cpu_timer;
    GPUTimer gpu_timer;
    gpu_timer.init();

    while (!glfwWindowShouldClose(window)) {

        // partenza timer CPU e GPU
        cpu_timer.start();
        gpu_timer.start();

        glm::vec3 lightDir = glm::vec3(r.sunlight_direction().x, r.sunlight_direction().y, r.sunlight_direction().z);
        view = camera.get_view_matrix();
		glm::vec3 sun_color = get_sun_color(lightDir);
        glUniformMatrix4fv(current_program["uView"], 1, GL_FALSE, &view[0][0]);
        glUniform3f(current_program["uSunDir"], lightDir.x, lightDir.y, lightDir.z);
        glUniform3f(current_program["uSunColor"], sun_color.x, sun_color.y, sun_color.z);
		glm::vec3 viewPos = camera.get_position();
        glUniform3fv(current_program["uViewPos"], 1, &viewPos[0]);

        glClearColor(0.3f, 0.3f, 0.3f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        check_gl_errors(__LINE__, __FILE__);
        r.update();
        stack.load_identity();
        stack.push();
        stack.mult(tb[0].matrix());

        float s = 1.f / r.bbox().diagonal();
        glm::vec3 c = r.bbox().center();
        stack.mult(glm::scale(glm::mat4(1.f), glm::vec3(s)));
        stack.mult(glm::translate(glm::mat4(1.f), -c));

        check_gl_errors(__LINE__, __FILE__);

		//r_sun_dir.bind();
		//stack.push();
  //      stack.mult(glm::scale(glm::mat4(1.f), glm::vec3(0.1f, 10.f, 0.1f)));
		//stack.mult(glm::translate(glm::mat4(1.f), glm::vec3(c.x, c.y, c.z))); 
		//glUniform3f((*current_program)["uColor"], 1.0, 1.0, 1.0);
  //      glUniformMatrix4fv((*current_program)["uModel"], 1, GL_FALSE, &stack.m()[0][0]);
  //      stack.pop();
		//glDrawElements(GL_TRIANGLE_STRIP, r_sun_dir().count, GL_UNSIGNED_INT, 0);


        glDepthRange(0.0, 1);
        render_terrain(r_terrain, stack, current_program, grass_texture.id);
        render_track(r_track, current_program, road_texture.id, road_normal_tex.id, r);

        render_cars(r, stack, current_program, car_bbox, car_objects);
        // draw trees from .glb file
        render_tree(r, stack, current_program, tree_bbox, tree_objects);

        render_cameramen(r, stack, current_program, camera_bbox, camera_objects);
        // draw street lamp from .glb file
		render_lamps(r, stack, current_program, lamp_bbox, lamp_objects);

        // --- Disegno della pista con la texture road.tile --
        check_gl_errors(__LINE__, __FILE__);
        gpu_timer.stop();
        cpu_timer.stop();

        // Aggiorna il risultato della GPU
        gpu_timer.update_results();

        /* draw the Graphical User Interface */
        ImGui_ImplGlfw_NewFrame();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Risorse");
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Separator();

        ImGui::Text("CPU Time: %.3f ms", cpu_timer.elapsed_ms);
        ImGui::Text("GPU Time: %.3f ms", gpu_timer.elapsed_ms);

        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        /* end of graphical user interface */

        stack.pop();
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    glUseProgram(0);
    glfwTerminate();
    return 0;
}
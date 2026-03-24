#define GLM_ENABLE_EXPERIMENTAL

#define NANOSVG_IMPLEMENTATION	// Expands implementation
#include "../3rdparty/nanosvg/src/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "../3rdparty/nanosvg/src/nanosvgrast.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

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
#include <algorithm>
#include <conio.h>
#include <direct.h>
#include "./common/matrix_stack.h"
#include "./common/intersection.h"
#include "./common/trackball.h"

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

    Camera() : position(0.f, 1.f, 1.5f), worldUp(0.f, 1.f, 0.f), yaw(-90.f), pitch(0.f), speed(0.05f), sensitivity(0.1f) {
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

    glm::mat4 get_view_matrix() const {
        return glm::lookAt(position, position + front, up);
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
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        tb[curr_tb].mouse_press(proj, view, xpos, ypos);
    }
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
        tb[curr_tb].mouse_release();
    }
}

/* callback function called when a mouse wheel is rotated */
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    if (curr_tb == 0)
        tb[0].mouse_scroll(xoffset, yoffset);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        switch (key) {
        case GLFW_KEY_W: camera.move_forward();  break;
        case GLFW_KEY_S: camera.move_backward(); break;
        case GLFW_KEY_A: camera.move_left();     break;
        case GLFW_KEY_D: camera.move_right();    break;
        case GLFW_KEY_T: curr_tb = 1 - curr_tb;   break;
        case GLFW_KEY_R: camera = Camera();       break;
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(window, true); break;
        }
    }
}

int main(int argc, char** argv)
{
    race r;
    carousel_loader::load("./small_test.svg", "./terrain_256.png", r);
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
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwMakeContextCurrent(window);
    glewInit();
    glEnable(GL_MULTISAMPLE);
    printout_opengl_glsl_info();

    renderable fram = shape_maker::frame();
    renderable r_cube = shape_maker::cube();
    renderable r_track; r_track.create(); game_to_renderable::to_track(r, r_track);
    renderable r_terrain; r_terrain.create(); game_to_renderable::to_heightfield(r, r_terrain); // 5.0f = numero ripetizioni della texture
    std::cout << "VAO: " << r_terrain.vao << std::endl;
    std::cout << "Index buffer (EBO): " << r_terrain().ind << std::endl;
    std::cout << "Index count: " << r_terrain().count << std::endl;

    renderable r_trees; r_trees.create(); game_to_renderable::to_tree(r, r_trees);
    renderable r_lamps; r_lamps.create(); game_to_renderable::to_lamps(r, r_lamps);

    shader basic_shader;
    basic_shader.create_program("shaders/basic.vert", "shaders/basic.frag");
    glUseProgram(basic_shader.program);
    
    // --- Caricamento texture tileabile per l'erba ---
    int tex_width, tex_height, tex_channels;
    unsigned char* tex_data = stbi_load("./grass_tile.PNG", &tex_width, &tex_height, &tex_channels, STBI_rgb_alpha);
    if (!tex_data) {
        std::cerr << "Errore nel caricamento della texture!" << std::endl;
        return -1;
    }

    GLuint tex_id;
    glGenTextures(1, &tex_id);
    glBindTexture(GL_TEXTURE_2D, tex_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_width, tex_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex_data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(tex_data);


    // --- Caricamento texture della  pista ---
    int road_width, road_height, road_channels;
    unsigned char* road_data = stbi_load("./street_tile.PNG", &road_width, &road_height, &road_channels, STBI_rgb_alpha);
    if (!road_data) {
        std::cerr << "Errore nel caricamento della texture della pista!" << std::endl;
        return -1;
    }

    GLuint road_tex_id;
    glGenTextures(1, &road_tex_id);
    glBindTexture(GL_TEXTURE_2D, road_tex_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, road_width, road_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, road_data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(road_data);


    glViewport(0, 0, 800, 800);

    tb[0].reset();
    tb[0].set_center_radius(glm::vec3(0, 0, 0), 1.f);
    curr_tb = 0;

    proj = glm::perspective(glm::radians(45.f), 1.f, 0.1f, 100.f); //modificato

    glUniformMatrix4fv(basic_shader["uProj"], 1, GL_FALSE, &proj[0][0]);

    r.start(11, 0, 0, 600);
    r.update();
    matrix_stack stack;
    glEnable(GL_DEPTH_TEST);

    while (!glfwWindowShouldClose(window)) {
        view = camera.get_view_matrix();
        glUniformMatrix4fv(basic_shader["uView"], 1, GL_FALSE, &view[0][0]);

        glClearColor(0.3f, 0.3f, 0.3f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        check_gl_errors(__LINE__, __FILE__);
        r.update();
        stack.load_identity();
        stack.push();
        stack.mult(tb[0].matrix());
        glUniformMatrix4fv(basic_shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);
        


        glColor3f(0, 0, 1);
        glBegin(GL_LINES);
        glVertex3f(0, 0, 0);
        glVertex3f(r.sunlight_direction().x, r.sunlight_direction().y, r.sunlight_direction().z);
        glEnd();

        float s = 1.f / r.bbox().diagonal();
        glm::vec3 c = r.bbox().center();
        stack.mult(glm::scale(glm::mat4(1.f), glm::vec3(s)));
        stack.mult(glm::translate(glm::mat4(1.f), -c));
        glDepthRange(0.01, 1);
        glUniformMatrix4fv(basic_shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);
        // --- Disegno del terreno con la texture grass.tile ---
        glUniform3f(basic_shader["uColor"], -1.f, 0.f, 0.5f); // forziamo l'uso della texture

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex_id);
        glUniform1i(basic_shader["uTex"], 0); // texture unit 0

        


        r_terrain.bind();
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDrawElements(GL_TRIANGLES, r_terrain().count, GL_UNSIGNED_INT, 0);
        std::cout << "Indice count: " << r_terrain().count << std::endl;

        


        // --- Disegno della pista con la texture road.tile ---
        glUniformMatrix4fv(basic_shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);
        glUniform3f(basic_shader["uColor"], -1.f, 0.f, 0.5f); // forza uso texture

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, road_tex_id); // usa texture della pista
        glUniform1i(basic_shader["uTex"], 0);      // bind texture unit 0

        // --- prova per evitare z-fighting con il terreno
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-20.0f, -20.0f);

        r_track.bind();
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDrawElements(GL_TRIANGLES, r_track().count, GL_UNSIGNED_INT, 0);

        glDisable(GL_POLYGON_OFFSET_FILL); //fine ossfet



        glDepthRange(0.0, 1);

        for (unsigned int ic = 0; ic < r.cars().size(); ++ic) {
            stack.push();
            stack.mult(r.cars()[ic].frame);
            stack.mult(glm::translate(glm::mat4(1.f), glm::vec3(0, 0.1, 0.0)));
            glUniformMatrix4fv(basic_shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);
            glUniform3f(basic_shader["uColor"], -1.f, 0.6f, 0.f);
            fram.bind();
            glDrawArrays(GL_LINES, 0, 6);
            stack.pop();
        }

        fram.bind();
        for (unsigned int ic = 0; ic < r.cameramen().size(); ++ic) {
            stack.push();
            stack.mult(r.cameramen()[ic].frame);
            stack.mult(glm::scale(glm::mat4(1.f), glm::vec3(4, 4, 4)));
            glUniformMatrix4fv(basic_shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);
            glUniform3f(basic_shader["uColor"], -1.f, 0.6f, 0.f);
            glDrawArrays(GL_LINES, 0, 6);
            stack.pop();
        }
        glUniformMatrix4fv(basic_shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);
        r_track.bind();
        glPointSize(3.0);
        glUniform3f(basic_shader["uColor"], 0.2f, 0.3f, 0.2f);
        glDrawArrays(GL_LINE_STRIP, 0, r_track.vn);
        glPointSize(1.0);

        glUniformMatrix4fv(basic_shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);

        r_trees.bind();
        glUniform3f(basic_shader["uColor"], 0.f, 1.0f, 0.f);
        glDrawArrays(GL_LINES, 0, r_trees.vn);

        r_lamps.bind();
        glUniform3f(basic_shader["uColor"], 1.f, 1.0f, 0.f);
        glDrawArrays(GL_LINES, 0, r_lamps.vn);

        stack.pop();
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    glUseProgram(0);
    glfwTerminate();
    return 0;
}
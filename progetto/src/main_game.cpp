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

const unsigned int SRC_WIDTH = 800;
const unsigned int SRC_HEIGHT = 800;

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

    glm::mat4 get_view_matrix(matrix_stack& stack){

        if (cameraman_view) {
            if (cameraman_idx >= r.cameramen().size()) {
				cameraman_idx = 0;
				cameraman_view = false;
            }
            float s = 1.f / r.bbox().diagonal();
            glm::vec3 center = r.bbox().center();
            cameraman c = r.cameramen()[cameraman_idx];
            glm::mat4 frame = c.frame;
            frame = glm::translate(frame, glm::vec3(0.0f, 2.f, 3.f));
            frame = glm::translate(glm::mat4(1), -center) * frame;
            frame[3] = glm::scale(glm::mat4(1), glm::vec3(s)) * frame[3] + glm::vec4(0, 0.005, 0, 0);
            return glm::inverse(frame);
		}
        return glm::lookAt(position, position + front, up);
    }

    glm::mat4 get_projection_matrix(float screen_width, float screen_height) {
        // Calcola l'aspect ratio
        float aspect_ratio = screen_width / screen_height;

        if (cameraman_view) {

            float pov_fov = 60.f;

			return glm::perspective(glm::radians(pov_fov), aspect_ratio, 0.01f, 10.0f);
        }
        return glm::perspective(glm::radians(Zoom), aspect_ratio, 0.1f, 50.0f);
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

struct uniform_material {
    glm::vec4  uColor;
    float uMetallic;
    float uRoughness;
    float uOcclusionStrength;
    float alpha_cutoff;
    int   alpha_mode;
    float padding[3];
};


struct uniform_light {
    glm::vec3 position;
    float intensity;
    glm::vec3 color;
    float cutOff;
    glm::vec3 direction;
    float outerCutOff;
};


struct light_uniform_locations {
    GLint position;
    GLint direction;
    GLint color;
    GLint intensity;
    GLint cutOff;
	GLint outerCutOff;
};


struct lamp_light_uniform_locations {
    GLint position;
    GLint color;
    GLint intensity;
};

// --- GLFW Callbacks ---
static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);

// --- Texture Utilities ---
GLuint createDummyTexture(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
void initDummyTextures(GLuint& dummyWhiteTex, GLuint& dummyBlackTex, GLuint& dummyNormalTex);
void send_pbr_texture(shader& shader);

// --- Lighting & Environment ---
glm::vec3 get_sun_color(const glm::vec3& sun_dir);
void init_car_lights(uniform_light car_lights[], box3 car_bbox);
void update_car_lights(matrix_stack& stack, uniform_light car_lights[], box3 car_bbox);
void init_lamp_lights(uniform_light lamp_lights[], box3 lamp_bbox);
void update_lamp_lights(matrix_stack& stack, uniform_light lamp_lights[], box3 lamp_bbox);

// --- Rendering Functions ---
void render_terrain(renderable& r_terrain, matrix_stack& stack, shader& shader, GLuint grass_texture_id, std::vector<int>& m_idx);
void render_terrain_shadows(renderable& r_terrain, matrix_stack& stack, shader& shader);
void render_track(renderable& r_track, matrix_stack& stack, shader& shader, GLuint road_texture_id, GLuint road_normal_id, std::vector<int>& m_idx, int offset);
void render_tree(matrix_stack& stack, shader& shader, box3 tree_bbox, std::vector<renderable>& tree_objects, std::vector<int>& m_idx, int offset);
void render_tree_shadows(matrix_stack& stack, shader& shader, box3 tree_bbox, std::vector<renderable>& tree_objects);
void render_lamps(matrix_stack& stack, shader& shader, box3 lamp_bbox, std::vector<renderable>& lamp_objects, std::vector<int>& m_idx, int offset);
void render_lamps_shadows(matrix_stack& stack, shader& shader, box3 lamp_bbox, std::vector<renderable>& lamp_objects);
void render_cameramen(matrix_stack& stack, shader& shader, box3 camera_bbox, std::vector<renderable>& camera_objects, std::vector<int>& m_idx, int offset);
void render_cameramen_shadows(matrix_stack& stack, shader& shader, box3 camera_bbox, std::vector<renderable>& camera_objects);
void render_cars(matrix_stack& stack, shader& shader, box3 car_bbox, std::vector<renderable>& car_objects, std::vector<int>& m_idx, int offset);
void render_cars_shadows(matrix_stack& stack, shader& shader, box3 car_bbox, std::vector<renderable>& car_objects);
void render_cars_blending(matrix_stack& stack, shader& shader, box3 car_bbox, std::vector<renderable>& car_objects, std::vector<int>& m_idx, std::vector<int>& b_idx);

// --- Matrix & Material Utilities ---
glm::mat4 build_lamp_model_matrix(const race& r, unsigned int lamp_index, const box3& lamp_bbox);
void push_back_material(uniform_material* m, std::vector<renderable> objects, std::vector<int>& material_idx, std::vector<int>& blend_idx);
void set_track_ubo(uniform_material* m, std::vector<int>& material_idx, int idx);

int main(int argc, char** argv)
{
    carousel_loader::load("./assets/small_test.svg", "./assets/terrain_256.png", r);
    for (int i = 0; i < 10; ++i)
        r.add_car();

    GLFWwindow* window;
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_SAMPLES, 4);
    window = glfwCreateWindow(SRC_WIDTH, SRC_HEIGHT, "CarOusel", NULL, NULL);
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
    renderable r_terrain; r_terrain.create(); game_to_renderable::to_heightfield(r, r_terrain);
	renderable r_sun_dir = shape_maker::cylinder(6, 0.05f);
    std::cout << "VAO: " << r_terrain.vao << std::endl;
    std::cout << "Index buffer (EBO): " << r_terrain().ind << std::endl;
    std::cout << "Index count: " << r_terrain().count << std::endl;

	initDummyTextures(dummyWhiteTex, dummyBlackTex, dummyNormalTex);

    uniform_material materials[18];
    std::vector<int> material_idx;
    std::vector<int> blend_idx;
    unsigned int idx = 0;

    gltf_loader gltfL_car;
    gltfL_car.assignDummyTexture(&dummyWhiteTex, &dummyBlackTex, &dummyNormalTex);
    std::vector<renderable> car_objects;
    box3 car_bbox;
    gltfL_car.load_to_renderable("assets/modelli/car_low-poly.glb", car_objects, car_bbox);
    push_back_material(&materials[idx], car_objects, material_idx, blend_idx);
    idx += car_objects.size();

    gltf_loader gltfL_camera;
    gltfL_camera.assignDummyTexture(&dummyWhiteTex, &dummyBlackTex, &dummyNormalTex);
    std::vector<renderable> camera_objects;
    box3 camera_bbox;
    gltfL_camera.load_to_renderable("assets/modelli/camera2.glb", camera_objects, camera_bbox);
    push_back_material(&materials[idx], camera_objects, material_idx, blend_idx);
    idx += camera_objects.size();

    gltf_loader gltfL_lamp;
    gltfL_lamp.assignDummyTexture(&dummyWhiteTex, &dummyBlackTex, &dummyNormalTex);
    std::vector<renderable> lamp_objects;
    box3 lamp_bbox;
    gltfL_lamp.load_to_renderable("assets/modelli/low_poly_lamp.glb", lamp_objects, lamp_bbox);
    push_back_material(&materials[idx], lamp_objects, material_idx, blend_idx);
    idx += lamp_objects.size();

    set_track_ubo(&materials[idx], material_idx, idx);
    idx++;

    gltf_loader gltfL_tree;
    gltfL_tree.assignDummyTexture(&dummyWhiteTex, &dummyBlackTex, &dummyNormalTex);
    std::vector<renderable> tree_objects;
    box3 tree_bbox;
    gltfL_tree.load_to_renderable("assets/modelli/low_poly_tree.glb", tree_objects, tree_bbox);
    push_back_material(&materials[idx], tree_objects, material_idx, blend_idx);
    idx += tree_objects.size();

    printf("idx: %d", idx);

    shader shadow_shader;
	shadow_shader.create_program("shaders/shadow.vert", "shaders/shadow.frag");

    shader tree_shader;
    tree_shader.create_program("shaders/light.vert", "shaders/tree.frag");
	glUseProgram(tree_shader.program);
	glUniform1i(tree_shader["uTex"], 0);
	glUniform1i(tree_shader["uNormalTex"], 2);
    glUniform1i(tree_shader["uShadowMap"], 5);

    shader terrain_shader;
	terrain_shader.create_program("shaders/light.vert", "shaders/terrain.frag");
	glUseProgram(terrain_shader.program);
    glUniform1i(terrain_shader["uTex"], 0);
	glUniform1i(terrain_shader["uShadowMap"], 5);

    shader model_shader;
    model_shader.create_program("shaders/light.vert", "shaders/model.frag");
	shader current_program;

    // --- Caricamento texture tileabile per l'erba ---
	texture grass_texture;
	grass_texture.load("./assets/texture/grass_tile.PNG", 0);
    // --- Caricamento texture della  pista ---
	texture road_texture;
	road_texture.load("./assets/texture/street_tile2.PNG", 0);

	texture road_normal_tex;
	road_normal_tex.load("./assets/texture/normal_map.JPG", 1);

	std::string path = "./assets/texture/skybox/Daylight Box";
	texture skybox_texture;
	skybox_texture.load_cubemap( path + "_Right.bmp", path + "_Left.bmp", path + "_Top.bmp", path + "_Bottom.bmp", path + "_Front.bmp", path + "_Back.bmp" , 0);

    glViewport(0, 0, 800, 800);

    tb[0].reset();
    tb[0].set_center_radius(glm::vec3(0, 0, 0), 1.f);
    curr_tb = 0;


    proj = glm::perspective(glm::radians(45.f), 1.f, 0.1f, 50.f); //modificato
	glUseProgram(model_shader.program);
	current_program = model_shader;
    send_pbr_texture(model_shader);

    unsigned int ubo_material;
	glGenBuffers(1, &ubo_material);
	glBindBuffer(GL_UNIFORM_BUFFER, ubo_material);
	glBufferData(GL_UNIFORM_BUFFER, 18 * sizeof(uniform_material), nullptr, GL_STATIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, 1, ubo_material);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, 18 * sizeof(uniform_material), &materials[0]);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);


    // Creazione UBO per le luci
	uniform_light lights[40];
    init_lamp_lights(&lights[0], lamp_bbox);
	init_car_lights(&lights[20], car_bbox);

    unsigned int light_ubo;
    glGenBuffers(1, &light_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, light_ubo);
	glBufferData(GL_UNIFORM_BUFFER, 40 * sizeof(uniform_light) + 48, nullptr, GL_STATIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, 2, light_ubo);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, 40 * sizeof(uniform_light), &lights);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glm::vec4 light_info[3];

    // depth map
    const unsigned int SHADOW_WIDTH = 2048, SHADOW_HEIGHT = 2048;

    unsigned int depthMap;
    glGenTextures(1, &depthMap);
    glBindTexture(GL_TEXTURE_2D, depthMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT,
        SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);


    // frame buffer per depth map
    unsigned int depthMapFBO;
    glGenFramebuffers(1, &depthMapFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glm::mat4 lightProjection = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 1.0f, 100.0f);
    glm::mat4 lightView = glm::mat4(0.0f);
	glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    unsigned int ubo;
    glGenBuffers(1, &ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(glm::mat4) * 3, nullptr, GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat4), &proj[0][0]);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);


    r.start(11, 0, 0, 600);
    r.update();

    matrix_stack stack;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // inizializzazione per misura delle risorse
    CPUTimer cpu_timer;
    GPUTimer gpu_timer;
    gpu_timer.init();
    while (!glfwWindowShouldClose(window)) {

        int offset = 0;
        // partenza timer CPU e GPU
        cpu_timer.start();
        gpu_timer.start();

        glm::vec3 lightDir = glm::vec3(r.sunlight_direction().x, r.sunlight_direction().y, r.sunlight_direction().z);
		light_info[0] = glm::vec4(lightDir, 0.0);
		glm::vec3 sun_color = get_sun_color(lightDir);
		light_info[1] = glm::vec4(sun_color, 1.0f);
        view = camera.get_view_matrix(stack);
		glm::vec3 viewPos = camera.get_position();
		light_info[2] = glm::vec4(viewPos, 1.0f);

        // aggiorna le informazioni sulla luce del sole, e la viewPos
        glBindBuffer(GL_UNIFORM_BUFFER, light_ubo);
        glBufferSubData(GL_UNIFORM_BUFFER, 40 * sizeof(uniform_light), sizeof(light_info), &light_info[0]);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        proj = camera.get_projection_matrix(SRC_WIDTH, SRC_HEIGHT);

        float tb_scale = glm::length(glm::vec3(tb[0].matrix()[0]));
        float scene_radius = 0.5f * tb_scale;
        float margin = scene_radius * 1.2f;

        glm::vec3 sun_pos = lightDir * (margin * 2.0f);
        lightView = glm::lookAt(sun_pos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        lightProjection = glm::ortho(
            -margin, margin,  // Left, Right
            -margin, margin,  // Bottom, Top
            0.1f,              // Near plane
            margin * 4.0f      // Far plane (lungo abbastanza da attraversare tutta la pista)
        );

        lightSpaceMatrix = lightProjection * lightView;

        // Aggiorna la matrice di vista e la matrice lightSpace
        glBindBuffer(GL_UNIFORM_BUFFER, ubo);
		glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat4), &proj[0][0]);
        glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat4), sizeof(glm::mat4), &view[0][0]);
        glBufferSubData(GL_UNIFORM_BUFFER, 2 * sizeof(glm::mat4), sizeof(glm::mat4), &lightSpaceMatrix[0][0]);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

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

        glDepthRange(0.0, 1);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

		// aggiorna le posizioni e direzioni dei fari delle auto
		glBindBuffer(GL_UNIFORM_BUFFER, light_ubo);
		update_lamp_lights(stack, &lights[0], lamp_bbox);
		update_car_lights(stack, &lights[20], car_bbox);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, 40 * sizeof(uniform_light), &lights[0]);
		glBindBuffer(GL_UNIFORM_BUFFER, 0);

		// Render pass per la shadow map
		glBindBuffer(GL_UNIFORM_BUFFER, ubo);
        glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
		glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
        glClear(GL_DEPTH_BUFFER_BIT);
		current_program = shadow_shader;
        glUseProgram(current_program.program);
        glCullFace(GL_FRONT);

        render_cars_shadows(stack, current_program, car_bbox, car_objects);
        render_cameramen_shadows(stack, current_program, camera_bbox, camera_objects);
        render_lamps_shadows(stack, current_program, lamp_bbox, lamp_objects);
        render_tree_shadows(stack, current_program, tree_bbox, tree_objects);
        render_terrain_shadows(r_terrain, stack, current_program);

		glBindBuffer(GL_UNIFORM_BUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, depthMap);
		current_program = model_shader;
		glUseProgram(current_program.program);
        glBindBuffer(GL_UNIFORM_BUFFER, ubo_material);

        glCullFace(GL_BACK);
        glViewport(0, 0, SRC_WIDTH, SRC_HEIGHT);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


        render_cars(stack, current_program, car_bbox, car_objects, material_idx, offset);
        offset += car_objects.size();
        render_cameramen(stack, current_program, camera_bbox, camera_objects, material_idx, offset);
        offset += camera_objects.size();
		render_lamps(stack, current_program, lamp_bbox, lamp_objects, material_idx, offset);
        offset += lamp_objects.size();
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-2.f, -2.f);
        render_track(r_track, stack, current_program, road_texture.id, road_normal_tex.id, material_idx, offset);
		glDisable(GL_POLYGON_OFFSET_FILL);
        offset += 1;

        current_program = tree_shader;
		glUseProgram(current_program.program);
        render_tree(stack, current_program, tree_bbox, tree_objects, material_idx, offset);
        offset += tree_objects.size();

		current_program = terrain_shader;
		glUseProgram(current_program.program);
		glDisable(GL_CULL_FACE);
        render_terrain(r_terrain, stack, current_program, grass_texture.id, material_idx);
		glEnable(GL_CULL_FACE);

        glBindBuffer(GL_UNIFORM_BUFFER, 0);

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
        case GLFW_KEY_T: tb[0].reset();   break;
        case GLFW_KEY_R: camera.cameraman_view = false; camera = Camera(); tb[0].reset(); break;
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


void render_terrain(renderable& r_terrain, matrix_stack& stack, shader& shader, GLuint grass_texture_id, std::vector<int>& m_idx) {

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, grass_texture_id);

    glDepthRange(0.01, 1);

    glUniformMatrix4fv(shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);
    r_terrain.bind();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDrawElements(GL_TRIANGLES, r_terrain().count, GL_UNSIGNED_INT, 0);
}


void render_terrain_shadows(renderable& r_terrain, matrix_stack& stack, shader& shader) {

    glDepthRange(0.01, 1);

    glUniformMatrix4fv(shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);
    r_terrain.bind();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDrawElements(GL_TRIANGLES, r_terrain().count, GL_UNSIGNED_INT, 0);

}


void render_track(renderable& r_track, matrix_stack& stack, shader& shader, GLuint road_texture_id, GLuint road_normal_id, std::vector<int>& m_idx, int offset) {

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, road_texture_id);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, dummyWhiteTex);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, road_normal_id);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, dummyBlackTex);

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, dummyWhiteTex);

    glUniform1i(shader["indice"], m_idx[offset]);
    glUniformMatrix4fv(shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);
    r_track.bind();
    glDrawElements(GL_TRIANGLES, r_track().count, GL_UNSIGNED_INT, 0);
}


void render_tree(matrix_stack& stack, shader& shader, box3 tree_bbox, std::vector<renderable>& tree_objects, std::vector<int>& m_idx, int offset) {
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

            stack.mult(tree_objects[j].transform);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tree_objects[j].mater.base_color_texture);

            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, tree_objects[j].mater.normal_texture);

            glUniform1i(shader["indice"], m_idx[offset + j]);
            glUniformMatrix4fv(shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);

            glDrawElements(tree_objects[j]().mode, tree_objects[j]().count, tree_objects[j]().itype, 0);
            stack.pop();
        }
        stack.pop();
    }
}


void render_tree_shadows(matrix_stack& stack, shader& shader, box3 tree_bbox, std::vector<renderable>& tree_objects) {
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
            stack.mult(tree_objects[j].transform);

            glUniformMatrix4fv(shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);
            glDrawElements(tree_objects[j]().mode, tree_objects[j]().count, tree_objects[j]().itype, 0);

            stack.pop();
        }
        stack.pop();
    }
}


glm::mat4 build_lamp_model_matrix(const race& r, unsigned int lamp_index, const box3& lamp_bbox) {
    glm::mat4 model_matrix = glm::translate(glm::mat4(1.0f), r.lamps()[lamp_index].pos);

    if (lamp_index < r.lamps().size() / 2) {
        model_matrix = glm::rotate(model_matrix, glm::radians(180.0f), glm::vec3(0, 1, 0));
    }

    float scale = 1.0f / lamp_bbox.diagonal();
    model_matrix = glm::scale(model_matrix, glm::vec3(7.3f * scale));
    return model_matrix;
}


void render_lamps(matrix_stack& stack, shader& shader, box3 lamp_bbox, std::vector<renderable>& lamp_objects, std::vector<int>& m_idx, int offset) {
    for (unsigned int i = 0; i < r.lamps().size(); ++i) {
        stack.push();

        stack.mult(build_lamp_model_matrix(r, i, lamp_bbox));

        for (unsigned int j = 0; j < lamp_objects.size(); ++j) {
            lamp_objects[j].bind();
            stack.push();

            stack.mult(lamp_objects[j].transform);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, lamp_objects[j].mater.base_color_texture);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, lamp_objects[j].mater.metallic_roughness_texture);

            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, lamp_objects[j].mater.normal_texture);

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, lamp_objects[j].mater.emissive_texture);

            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, lamp_objects[j].mater.occlusion_texture);

            glUniform1i(shader["indice"], m_idx[offset + j]);
            glUniformMatrix4fv(shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);

            glDrawElements(lamp_objects[j]().mode, lamp_objects[j]().count, lamp_objects[j]().itype, 0);
            stack.pop();
        }
        stack.pop();
    }
}


void render_lamps_shadows(matrix_stack& stack, shader& shader, box3 lamp_bbox, std::vector<renderable>& lamp_objects) {
    for (unsigned int i = 0; i < r.lamps().size(); ++i) {
        stack.push();
        stack.mult(build_lamp_model_matrix(r, i, lamp_bbox));

        for (unsigned int j = 0; j < lamp_objects.size(); ++j) {
            lamp_objects[j].bind();
            stack.push();
            stack.mult(lamp_objects[j].transform);

            glUniformMatrix4fv(shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);
            glDrawElements(lamp_objects[j]().mode, lamp_objects[j]().count, lamp_objects[j]().itype, 0);

            stack.pop();
        }
        stack.pop();
    }
}


void render_cameramen(matrix_stack& stack, shader& shader, box3 camera_bbox, std::vector<renderable>& camera_objects, std::vector<int>& m_idx, int offset) {
    for (unsigned int ic = 0; ic < r.cameramen().size(); ++ic) {
        if (camera.cameraman_view && ic == camera.cameraman_idx) {
            continue;
        }
        stack.push();
        stack.mult(r.cameramen()[ic].frame);
        float scale = 1.f / camera_bbox.diagonal();
        stack.mult(glm::scale(glm::mat4(1.f), glm::vec3(scale * 2.2f)));
        stack.mult(glm::rotate(glm::mat4(1.f), glm::radians(90.f), glm::vec3(0, 1, 0)));
        stack.mult(glm::translate(glm::mat4(1.f), glm::vec3(-camera_bbox.center().x, -camera_bbox.min.y, -camera_bbox.center().z)));

        for (unsigned int j = 0; j < camera_objects.size(); ++j) {
            camera_objects[j].bind();
            stack.push();
            stack.mult(camera_objects[j].transform);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, camera_objects[j].mater.base_color_texture);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, camera_objects[j].mater.metallic_roughness_texture);

            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, camera_objects[j].mater.normal_texture);

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, camera_objects[j].mater.emissive_texture);

            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, camera_objects[j].mater.occlusion_texture);

            glUniform1i(shader["indice"], m_idx[offset + j]);
            glUniformMatrix4fv(shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);

            glDrawElements(camera_objects[j]().mode, camera_objects[j]().count, camera_objects[j]().itype, 0);
            stack.pop();
        }
        stack.pop();
    }
}


void render_cameramen_shadows(matrix_stack& stack, shader& shader, box3 camera_bbox, std::vector<renderable>& camera_objects) {
    for (unsigned int ic = 0; ic < r.cameramen().size(); ++ic) {
        if (camera.cameraman_view && ic == camera.cameraman_idx) {
            continue; // Non disegniamo l'ombra del cameraman se lo stiamo impersonando
        }
        stack.push();
        stack.mult(r.cameramen()[ic].frame);
        float scale = 1.f / camera_bbox.diagonal();
        stack.mult(glm::scale(glm::mat4(1.f), glm::vec3(scale * 2.2f)));
        stack.mult(glm::rotate(glm::mat4(1.f), glm::radians(90.f), glm::vec3(0, 1, 0)));
        stack.mult(glm::translate(glm::mat4(1.f), glm::vec3(-camera_bbox.center().x, -camera_bbox.min.y, -camera_bbox.center().z)));

        for (unsigned int j = 0; j < camera_objects.size(); ++j) {
            camera_objects[j].bind();
            stack.push();
            stack.mult(camera_objects[j].transform);

            glUniformMatrix4fv(shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);
            glDrawElements(camera_objects[j]().mode, camera_objects[j]().count, camera_objects[j]().itype, 0);

            stack.pop();
        }
        stack.pop();
    }
}


void render_cars(matrix_stack& stack, shader& shader, box3 car_bbox, std::vector<renderable>& car_objects, std::vector<int>& m_idx, int offset) {

    for (unsigned int ic = 0; ic < r.cars().size(); ++ic) {
        stack.push();
        stack.mult(r.cars()[ic].frame);
        glm::vec3 car_pos = glm::vec3(r.cars()[ic].frame[3].x, r.cars()[ic].frame[3].y, r.cars()[ic].frame[3].z);
        float car_height = car_bbox.max.y - car_bbox.min.y;
        float scale = 1.f / car_bbox.diagonal();

        stack.mult(glm::scale(glm::mat4(1.f), glm::vec3(5.f * scale)));

        stack.mult(glm::rotate(glm::mat4(1.f), glm::radians(180.f), glm::vec3(0, 1, 0)));

        stack.mult(glm::translate(glm::mat4(1.f), glm::vec3(-car_bbox.center().x, -car_bbox.center().y - car_height * 0.1f, -car_bbox.center().z)));

        for (unsigned int j = 0; j < car_objects.size(); j++) {
            car_objects[j].bind();
            stack.push();
            stack.mult(car_objects[j].transform);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, car_objects[j].mater.base_color_texture);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, car_objects[j].mater.metallic_roughness_texture);

            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, car_objects[j].mater.normal_texture);

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, car_objects[j].mater.emissive_texture);

            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, car_objects[j].mater.occlusion_texture);

            glUniform1i(shader["indice"], m_idx[offset + j]);
            glUniformMatrix4fv(shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);
            glDrawElements(car_objects[j]().mode, car_objects[j]().count, car_objects[j]().itype, 0);
            stack.pop();
        }
        stack.pop();
    }
}


void render_cars_shadows(matrix_stack& stack, shader& shader, box3 car_bbox, std::vector<renderable>& car_objects) {
    for (unsigned int ic = 0; ic < r.cars().size(); ++ic) {
        stack.push();
        stack.mult(r.cars()[ic].frame);
        glm::vec3 car_pos = glm::vec3(r.cars()[ic].frame[3].x, r.cars()[ic].frame[3].y, r.cars()[ic].frame[3].z);
        float car_height = car_pos.y;
        float scale = 1.f / car_bbox.diagonal();

        stack.mult(glm::scale(glm::mat4(1.f), glm::vec3(5.f * scale)));
        stack.mult(glm::rotate(glm::mat4(1.f), glm::radians(180.f), glm::vec3(0, 1, 0)));
        stack.mult(glm::translate(glm::mat4(1.f), glm::vec3(-car_bbox.center().x, -car_bbox.min.y + 2.0f, -car_bbox.center().z)));

        for (unsigned int j = 0; j < car_objects.size(); j++) {
            car_objects[j].bind();
            stack.push();
            stack.mult(car_objects[j].transform);

            // Inviamo SOLO la matrice del modello, niente texture!
            glUniformMatrix4fv(shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);
            glDrawElements(car_objects[j]().mode, car_objects[j]().count, car_objects[j]().itype, 0);

            stack.pop();
        }
        stack.pop();
    }
}


void render_cars_blending(matrix_stack& stack, shader& shader, box3 car_bbox, std::vector<renderable>& car_objects, std::vector<int>& m_idx, std::vector<int>& b_idx) {

    for (unsigned int ic = 0; ic < r.cars().size(); ++ic) {
        stack.push();
        stack.mult(r.cars()[ic].frame);
        glm::vec3 car_pos = glm::vec3(r.cars()[ic].frame[3].x, r.cars()[ic].frame[3].y, r.cars()[ic].frame[3].z);
        float scale = 1.f / car_bbox.diagonal();
        stack.mult(glm::translate(glm::mat4(1.f), glm::vec3(-car_bbox.center().x, -car_bbox.min.y + 2.0f, -car_bbox.center().z)));
        stack.mult(glm::rotate(glm::mat4(1.f), glm::radians(180.f), glm::vec3(0, 1, 0)));
        stack.mult(glm::scale(glm::mat4(1.f), glm::vec3(7.3f * scale)));

        for (unsigned int j = 0; j < car_objects.size(); j++) {

            car_objects[j].bind();
            stack.push();
            stack.mult(car_objects[j].transform);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, car_objects[j].mater.base_color_texture);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, car_objects[j].mater.metallic_roughness_texture);

            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, car_objects[j].mater.normal_texture);

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, car_objects[j].mater.emissive_texture);

            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, car_objects[j].mater.occlusion_texture);

            glUniform1i(shader["indice"], m_idx[j]);

            glUniformMatrix4fv(shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);
            glDrawElements(car_objects[j]().mode, car_objects[j]().count, car_objects[j]().itype, 0);
            stack.pop();
        }
        stack.pop();
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    for (unsigned int ic = 0; ic < r.cars().size(); ++ic) {
        stack.push();
        // (Devi ricalcolare le matrici base dell'auto esattamente come sopra)
        stack.mult(r.cars()[ic].frame);
        float scale = 1.f / car_bbox.diagonal();
        stack.mult(glm::scale(glm::mat4(1.f), glm::vec3(5.f * scale)));
        stack.mult(glm::rotate(glm::mat4(1.f), glm::radians(180.f), glm::vec3(0, 1, 0)));
        stack.mult(glm::translate(glm::mat4(1.f), glm::vec3(-car_bbox.center().x, -car_bbox.min.y + 2.0f, -car_bbox.center().z)));

        for (unsigned int j = 0; j < b_idx.size(); j++) {

            car_objects[j].bind();
            stack.push();
            stack.mult(car_objects[j].transform);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, car_objects[j].mater.base_color_texture);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, car_objects[j].mater.metallic_roughness_texture);

            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, car_objects[j].mater.normal_texture);

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, car_objects[j].mater.emissive_texture);

            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, car_objects[j].mater.occlusion_texture);

            glUniform1i(shader["indice"], b_idx[j]);
            glUniformMatrix4fv(shader["uModel"], 1, GL_FALSE, &stack.m()[0][0]);
            glDrawElements(car_objects[j]().mode, car_objects[j]().count, car_objects[j]().itype, 0);
            stack.pop();
        }
        stack.pop();
    }

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}


void init_car_lights(uniform_light car_lights[], box3 car_bbox) {

    glm::vec3 color = glm::vec3(1.0f, 0.75f, 0.5f);

    for (unsigned int ic = 0; ic < r.cars().size(); ++ic) {
        int left_idx = ic * 2;
        int right_idx = ic * 2 + 1;

        // faro sinistro anteriore
        car_lights[left_idx].color = color;
        car_lights[left_idx].intensity = 10.0f;
        car_lights[left_idx].cutOff = glm::cos(glm::radians(12.5f));
        car_lights[left_idx].outerCutOff = glm::cos(glm::radians(17.5f));

        //faro destro anteriore
        car_lights[right_idx].color = color;
        car_lights[right_idx].intensity = 10.0f;
        car_lights[right_idx].cutOff = glm::cos(glm::radians(12.5f));
        car_lights[right_idx].outerCutOff = glm::cos(glm::radians(17.5f));
    }
}


void update_car_lights(matrix_stack& stack, uniform_light car_lights[], box3 car_bbox) {

    glm::vec4 forward_dir = glm::vec4(0.0f, -0.3f, 1.0f, 0.0f);

    float car_height = car_bbox.max.y - car_bbox.min.y;

    // Posizione locale dei fari rispetto al centro del modello 3D
    float faro_z = car_bbox.center().z + car_bbox.max.z * 0.5f;
    float faro_y = car_height * 0.5f;
    float faro_x = car_bbox.center().x + car_bbox.max.x * 0.75f;

    glm::vec4 left_light_pos = glm::vec4(faro_x, faro_y, faro_z, 1.0f);
    glm::vec4 right_light_pos = glm::vec4(-faro_x, faro_y, faro_z, 1.0f);

    for (unsigned int ic = 0; ic < r.cars().size(); ++ic) {
        stack.push();

        stack.mult(r.cars()[ic].frame);

        float scale = 1.f / car_bbox.diagonal();
        stack.mult(glm::scale(glm::mat4(1.f), glm::vec3(5.f * scale)));
        stack.mult(glm::rotate(glm::mat4(1.f), glm::radians(180.f), glm::vec3(0, 1, 0)));
        stack.mult(glm::translate(glm::mat4(1.f), glm::vec3(-car_bbox.center().x, car_height * 0.5f, -car_bbox.center().z)));

        glm::mat4 car_scene_model = stack.m();

        // Trasformiamo i punti e i vettori locali nello spazio globale della scena
        glm::vec3 final_dir = glm::normalize(glm::vec3(car_scene_model * forward_dir));
        glm::vec3 final_pos_left = glm::vec3(car_scene_model * left_light_pos);
        glm::vec3 final_pos_right = glm::vec3(car_scene_model * right_light_pos);

        int left_idx = ic * 2;
        int right_idx = ic * 2 + 1;

        // Faro Sinistro
        car_lights[left_idx].position = final_pos_left;
        car_lights[left_idx].direction = final_dir;

        // Faro Destro
        car_lights[right_idx].position = final_pos_right;
        car_lights[right_idx].direction = final_dir;

        stack.pop(); // Ripuliamo lo stack per la prossima auto
    }
}


void init_lamp_lights(uniform_light lamp_lights[], box3 lamp_bbox) {

    float innerAngle = glm::radians(10.f);
    float outerAngle = glm::radians(15.f);

    glm::vec3 dir = glm::vec3(0, -1, 0);

    glm::vec3 color = glm::vec3(1.0f, 0.75f, 0.5f);

    for (unsigned int i = 0; i < r.lamps().size(); ++i) {
        lamp_lights[i].direction = dir;
        lamp_lights[i].color = color;
        lamp_lights[i].intensity = 10.0f;
        lamp_lights[i].cutOff = glm::cos(innerAngle);
        lamp_lights[i].outerCutOff = glm::cos(outerAngle);
    }
}


void update_lamp_lights(matrix_stack& stack, uniform_light lamp_lights[], box3 lamp_bbox) {

    for (unsigned int i = 0; i < r.lamps().size(); ++i) {
        stack.push();
        stack.mult(glm::translate(glm::mat4(1.0f), glm::vec3(r.lamps()[i].pos.x, lamp_bbox.max.y - lamp_bbox.min.y, r.lamps()[i].pos.z)));
        float scale = 1.0f / lamp_bbox.diagonal();
        stack.mult(glm::scale(glm::mat4(1.0f), glm::vec3(7.3f * scale)));
        glm::mat4 lamp_model = stack.m();
        glm::vec3 light_pos = glm::vec3(lamp_model * glm::vec4(0, 1, 0, 1));
        lamp_lights[i].position = light_pos;
        stack.pop();
    }
}


void send_pbr_texture(shader& shader) {
    glUniform1i(shader["uTex"], 0);
    glUniform1i(shader["uMRTex"], 1);
    glUniform1i(shader["uNormalTex"], 2);
    glUniform1i(shader["uEmissive"], 3);
    glUniform1i(shader["uOcclusion"], 4);
    glUniform1i(shader["uShadowMap"], 5);
}


void push_back_material(uniform_material* m, std::vector<renderable> objects, std::vector<int>& material_idx, std::vector<int>& blend_idx) {

    for (int i = 0; i < objects.size(); i++) {
        renderable obj = objects[i];
        m[i].uColor = glm::vec4((float)obj.mater.base_color_factor[0], (float)obj.mater.base_color_factor[1], (float)obj.mater.base_color_factor[2], (float)obj.mater.base_color_factor[3]);
        m[i].uMetallic = obj.mater.metallic_factor;
        m[i].uRoughness = obj.mater.roughness_factor;
        m[i].alpha_cutoff = obj.mater.alpha_cutoff;
        m[i].uOcclusionStrength = obj.mater.occlusion_strength;
        if (obj.mater.alpha_mode == "BLEND") {
            m[i].alpha_mode = 2;
            blend_idx.push_back(i);
        }
        else {
            m[i].alpha_mode = obj.mater.alpha_mode == "MASK" ? 1 : 0;
            material_idx.push_back(i);
        }
    }
}


void set_track_ubo(uniform_material* m, std::vector<int>& material_idx, int idx) {
    m[0].uColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    m[0].uMetallic = 0.0f;
    m[0].uRoughness = 0.8f;
    m[0].alpha_cutoff = 0.0f;
    m[0].uOcclusionStrength = 1.0f;
    m[0].alpha_mode = 0;

    material_idx.push_back(idx);
}
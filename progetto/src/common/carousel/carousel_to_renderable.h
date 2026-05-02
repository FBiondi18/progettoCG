#pragma once

#include "..\renderable.h"
#include "carousel.h"



struct game_to_renderable {
	
	static void ct(float* dst, glm::vec3 src) {
		dst[0] = src.x;
		dst[1] = src.y;
		dst[2] = src.z;

	}
	static void get_normal(float* dst, glm::vec3 p1, glm::vec3 p2) {
		glm::vec3 n = glm::normalize(glm::cross(p2 - p1, glm::vec3(0, 0, 1)));
		dst[0] = n.x;
		dst[1] = n.y;
		dst[2] = n.z;
	}
	static void to_track(const race& r, renderable& r_t) {
		std::vector<float> buffer_track;
		std::vector<unsigned int> buffer_idx;

		size_t length = r.t().curbs[0].size();

		// Riserviamo spazio per 8 float (Pos, Norm, UV) ma per un vertice in più (length + 1)
		buffer_track.reserve((length + 1) * 16);
		buffer_idx.reserve(length * 6);

		float accumulated_distance = 0.0f;
		float texture_scale = 0.2f; // Regola la ripetizione dell'asfalto (es. 1.0f / larghezza_pista)

		// IMPORTANTE: il ciclo arriva fino a <= length per chiudere l'anello e sistemare le UV
		for (size_t i = 0; i <= length; ++i) {

			size_t current_idx = i % length;
			size_t prev_idx = (i + length - 1) % length;
			size_t next_idx = (i + 1) % length;

			glm::vec3 p_in = r.t().curbs[0][current_idx];
			glm::vec3 p_out = r.t().curbs[1][current_idx];

			// 1. Calcolo della distanza reale (invece dell'indice i) per evitare lo stretching
			if (i > 0) {
				glm::vec3 p_in_prev_real = r.t().curbs[0][prev_idx];
				glm::vec3 p_out_prev_real = r.t().curbs[1][prev_idx];
				glm::vec3 center_current = (p_in + p_out) * 0.5f;
				glm::vec3 center_prev = (p_in_prev_real + p_out_prev_real) * 0.5f;

				accumulated_distance += glm::length(center_current - center_prev);
			}

			glm::vec3 p_in_prev = r.t().curbs[0][prev_idx];
			glm::vec3 p_in_next = r.t().curbs[0][next_idx];

			// 2. Calcolo della Normale
			glm::vec3 Forward = glm::normalize(p_in_next - p_in_prev);
			glm::vec3 Right = glm::normalize(p_out - p_in);
			glm::vec3 N = glm::normalize(glm::cross(Forward, Right));

			// Coordinata V basata sulla distanza
			float v_coord = accumulated_distance * texture_scale;

			// --- VERTICE 1: Cordolo Interno ---
			buffer_track.push_back(p_in.x); buffer_track.push_back(p_in.y); buffer_track.push_back(p_in.z);
			buffer_track.push_back(N.x); buffer_track.push_back(N.y); buffer_track.push_back(N.z);
			buffer_track.push_back(1.0f); buffer_track.push_back(v_coord);

			// --- VERTICE 2: Cordolo Esterno ---
			buffer_track.push_back(p_out.x); buffer_track.push_back(p_out.y); buffer_track.push_back(p_out.z);
			buffer_track.push_back(N.x); buffer_track.push_back(N.y); buffer_track.push_back(N.z);
			buffer_track.push_back(0.0f); buffer_track.push_back(v_coord);

			// --- INDICI ---
			// Generiamo i triangoli solo fino all'ultimo segmento utile
			if (i < length) {
				GLuint idx1 = (2 * i);
				GLuint idx2 = (2 * i + 1);
				GLuint idx3 = (2 * (i + 1));
				GLuint idx4 = (2 * (i + 1)) + 1;

				buffer_idx.push_back(idx1);
				buffer_idx.push_back(idx3);
				buffer_idx.push_back(idx2);

				buffer_idx.push_back(idx3);
				buffer_idx.push_back(idx4);
				buffer_idx.push_back(idx2);
			}
		}

		int stride = 8 * sizeof(float);
		unsigned int total_elements = static_cast<unsigned int>(buffer_track.size());

		// L'assegnazione degli attributi torna ad essere quella che avevi già scritto tu!
		GLuint va_id = r_t.add_vertex_attribute<float>(&buffer_track[0], total_elements, 0, 3, stride, 0);
		r_t.assign_vertex_attribute(va_id, total_elements, 2, 3, GL_FLOAT, stride, 3 * sizeof(float));
		r_t.assign_vertex_attribute(va_id, total_elements, 4, 2, GL_FLOAT, stride, 6 * sizeof(float));
		r_t.add_indices<unsigned int>(&buffer_idx[0], static_cast<unsigned int>(buffer_idx.size()), GL_TRIANGLES);
	}

	static void to_stick_object(const std::vector<stick_object>& vec, renderable& r_t) {

		std::vector<float> buffer_pos;
		buffer_pos.resize((vec.size()*2) * 3 );
		for (unsigned int i = 0; i < vec.size();++i) {
			ct(&buffer_pos[(2 * i) * 3], vec[i].pos);
			ct(&buffer_pos[(2 * i + 1) * 3], vec[i].pos+glm::vec3(0, vec[i].height,0));

		}

		GLuint va_id = r_t.add_vertex_attribute<float>(&buffer_pos[0], static_cast<unsigned int>(buffer_pos.size()), 0, 3, 3 * sizeof(float), 0);

	}

	static void to_tree(const race& r, renderable& r_t) {
		to_stick_object(r.trees(), r_t);
	}
	static void to_lamps(const race& r, renderable& r_t) {
		to_stick_object(r.lamps(), r_t);
	}


	static void to_heightfield(const race& r, renderable& r_hf) {
		std::vector<unsigned int > buffer_id;
		const unsigned int& Z =static_cast<unsigned int>(r.ter().size_pix[1]);
		const unsigned int& X =static_cast<unsigned int>(r.ter().size_pix[0]);

		terrain ter = r.ter();

		std::vector<float>   hf3d;
		GLuint va_id;
		for (unsigned int iz = 0; iz < Z; ++iz)
			for (unsigned int ix = 0; ix < X; ++ix) {
				hf3d.push_back(ter.rect_xz[0] + (ix / float(X)) * ter.rect_xz[2]);
				hf3d.push_back(r.ter().hf(ix, iz));
				hf3d.push_back(ter.rect_xz[1] + (iz / float(Z)) * ter.rect_xz[3]);

				float dx = ter.rect_xz[2] / float(X); // size_x / width_pixels
				float dz = ter.rect_xz[3] / float(Z); // size_z / height_pixels

				// 2. Protezione dai bordi (fondamentale con gli unsigned int)
				unsigned int ix_L = (ix > 0) ? ix - 1 : 0;
				unsigned int ix_R = (ix < X - 1) ? ix + 1 : X - 1;
				unsigned int iz_D = (iz > 0) ? iz - 1 : 0;
				unsigned int iz_U = (iz < Z - 1) ? iz + 1 : Z - 1;

				// 3. Campionamento delle altezze tramite la tua funzione hf()
				float hL = ter.hf(ix_L, iz);
				float hR = ter.hf(ix_R, iz);
				float hD = ter.hf(ix, iz_D);
				float hU = ter.hf(ix, iz_U);

				// 4. Calcolo delle distanze effettive sui bordi
				float delta_x = (ix_R - ix_L) * dx;
				float delta_z = (iz_U - iz_D) * dz;

				// 5. Vettore Normale (Prodotto vettoriale)
				float nx = -delta_z * (hR - hL);
				float ny = delta_x * delta_z;
				float nz = -delta_x * (hU - hD);

				// Normalizzazione
				float len = std::sqrt(nx * nx + ny * ny + nz * nz);
				hf3d.push_back(nx / len);
				hf3d.push_back(ny / len);
				hf3d.push_back(nz / len);

				// aggiungo coordinate uv per la texture
				hf3d.push_back((ix / float(X)) * 5.f);
				hf3d.push_back((iz / float(Z)) * 5.f);


				if (iz < Z - 1 && ix < X - 1) {
					buffer_id.push_back((iz * X) + ix);
					buffer_id.push_back((iz * X) + ix + 1);
					buffer_id.push_back((iz + 1) * X + ix + 1);

					buffer_id.push_back((iz * X) + ix);
					buffer_id.push_back((iz + 1) * X + ix + 1);
					buffer_id.push_back((iz + 1) * X + ix);
				}
			}
		int stride = 8 * sizeof(float);
		int total_elements = X * Z * 8;

		va_id = r_hf.add_vertex_attribute<float>(&hf3d[0], total_elements, 0, 3, stride, 0);
		r_hf.assign_vertex_attribute(va_id, total_elements, 2, 3, GL_FLOAT, stride, 3 * sizeof(float));
		r_hf.assign_vertex_attribute(va_id, total_elements, 4, 2, GL_FLOAT, stride, 6 * sizeof(float));
		r_hf.add_indices<unsigned int>(&buffer_id[0], static_cast<unsigned int>(buffer_id.size()), GL_TRIANGLES);
	}

};
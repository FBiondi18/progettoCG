#pragma once

#include "..\renderable.h"
#include "carousel.h"



struct game_to_renderable {
	
	static void ct(float* dst, glm::vec3 src) {
		dst[0] = src.x;
		dst[1] = src.y;
		dst[2] = src.z;
	}
	static void to_track(const race & r, renderable& r_t)  {
		
		std::vector<float> buffer_pos;
		std::vector<unsigned int> buffer_idx;
		std::vector<float> buffer_uv;

		float track_length = static_cast<float>(r.t().curbs[0].size());
		float distanza = 0.f;
		GLuint idx1;
		GLuint idx2;
		GLuint idx3;
		GLuint idx4;

		buffer_pos.resize(r.t().curbs[0].size() * 2 * 3);
		buffer_uv.resize(r.t().curbs[0].size() * 2 * 2);
		buffer_idx.resize(r.t().curbs[0].size() * 2 * 3);
		size_t length = r.t().curbs[0].size();
		for (unsigned int i = 0; i < length ;++i) {
			ct(&buffer_pos[(2 * i  ) * 3], r.t().curbs[0][i % (length)]);
			ct(&buffer_pos[(2 * i+1) * 3], r.t().curbs[1][i % (length)]);

			// aggiungo coordinate uv per la texture
			distanza = float(i) / float(length);

			// Cordolo interno (Vertice 1)
			buffer_uv[(2 * i) * 2] = 1.0f; // U = 0.0 (Sinistra/Interno)
			buffer_uv[(2 * i) * 2 + 1] = distanza * 5.f;    // V = Avanzamento lungo la pista

			// Cordolo esterno (Vertice 2)
			buffer_uv[(2 * i + 1) * 2] = 0.0f; // U = 1.0 (Destra/Esterno)
			buffer_uv[(2 * i + 1) * 2 + 1] = distanza * 5.f;
			/*
			idx1 = (2 * i);
			idx2 = (2 * i + 1);
			idx3 = 2 * ((i + 1) % length);
			idx4 = 2 * ((i + 1) % length) + 1;


			buffer_idx.push_back(idx1);
			buffer_idx.push_back(idx2);
			buffer_idx.push_back(idx3);
			buffer_idx.push_back(idx3);
			buffer_idx.push_back(idx2);
			buffer_idx.push_back(idx4);
			*/
		}

		r_t.add_vertex_attribute<float>(&buffer_pos[0], static_cast<unsigned int>(buffer_pos.size()), 0, 3);
		r_t.add_vertex_attribute<float>(&buffer_uv[0], static_cast<unsigned int>(buffer_uv.size()), 2, 2);
		//r_t.add_indices<unsigned int>(&buffer_idx[0], static_cast<unsigned int>(buffer_idx.size()), GL_TRIANGLES);
	}

	static void to_stick_object(const std::vector<stick_object>& vec, renderable& r_t) {

		std::vector<float> buffer_pos;
		buffer_pos.resize((vec.size()*2) * 3 );
		for (unsigned int i = 0; i < vec.size();++i) {
			ct(&buffer_pos[(2 * i) * 3], vec[i].pos);
			ct(&buffer_pos[(2 * i+1) * 3], vec[i].pos+glm::vec3(0, vec[i].height,0));
		}

		r_t.add_vertex_attribute<float>(&buffer_pos[0], static_cast<unsigned int>(buffer_pos.size()), 0, 3);
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
		std::vector<float>   hf_uv;
		for (unsigned int iz = 0; iz < Z; ++iz)
			for (unsigned int ix = 0; ix < X; ++ix) {
				hf3d.push_back(ter.rect_xz[0] + (ix / float(X)) * ter.rect_xz[2]);
				hf3d.push_back(r.ter().hf(ix, iz));
				hf3d.push_back(ter.rect_xz[1] + (iz / float(Z)) * ter.rect_xz[3]);

				// aggiungo coordinate uv per la texture
				hf_uv.push_back((ix / float(X)) * 5.f);
				hf_uv.push_back((iz / float(Z)) * 5.f);
			}

		for (unsigned int iz = 0; iz < Z-1; ++iz)
			for (unsigned int ix = 0; ix < X-1; ++ix) {
				
				buffer_id.push_back((iz * Z) + ix);
				buffer_id.push_back((iz * Z) + ix + 1);
				buffer_id.push_back((iz + 1) * Z + ix + 1);

				buffer_id.push_back((iz * Z) + ix);
				buffer_id.push_back((iz + 1) * Z + ix + 1);
				buffer_id.push_back((iz + 1) * Z + ix);
			}

		r_hf.add_vertex_attribute<float>(&hf3d[0], X * Z * 3, 0, 3);
		r_hf.add_vertex_attribute<float>(&hf_uv[0], X * Z * 2, 2, 2);
		r_hf.add_indices<unsigned int>(&buffer_id[0], static_cast<unsigned int>(buffer_id.size()), GL_TRIANGLES);
	}

};
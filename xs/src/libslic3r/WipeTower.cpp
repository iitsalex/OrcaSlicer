#include "WipeTower.hpp"

#include <assert.h>
#include <math.h>
#include <fstream>
#include <iostream>

#ifdef __linux
#include <strings.h>
#endif /* __linux */

#ifdef _MSC_VER 
#define strcasecmp _stricmp
#endif

namespace PrusaSingleExtruderMM
{

class Writer
{
public:
	Writer() : 
		m_current_pos(std::numeric_limits<float>::max(), std::numeric_limits<float>::max()),
		m_current_z(0.f),
		m_current_feedrate(0.f),
		m_extrusion_flow(0.f) {}

	Writer&				 set_z(float z) 
		{ m_current_z = z; return *this; }

	Writer& 			 set_extrusion_flow(float flow)
		{ m_extrusion_flow = flow; return *this; }

	Writer& 			 feedrate(float f)
	{
		if (f != m_current_feedrate)
			m_gcode += "G1" + set_format_F(f) + "\n";
		return *this;
	}

	const std::string&   gcode() const { return m_gcode; }
	float                x()     const { return m_current_pos.x; }
	float                y()     const { return m_current_pos.y; }
	const WipeTower::xy& pos()   const { return m_current_pos; }

	Writer& extrude_explicit(float x, float y, float e, float f = 0.f) 
	{
		if (x == m_current_pos.x && y == m_current_pos.y && e == 0.f && (f == 0.f || f == m_current_feedrate))
			return *this;
		m_gcode += "G1 ";
		if (x != m_current_pos.x)
			m_gcode += set_format_X(x);
		if (y != m_current_pos.y)
			m_gcode += set_format_Y(y);
		if (e != 0)
			m_gcode += set_format_E(e);
		if (f != 0 && f != m_current_feedrate)
			m_gcode += set_format_F(f);
		m_gcode += "\n";
		return *this;
	}

	Writer& extrude_explicit(const WipeTower::xy &dest, float e, float f = 0.f) 
		{ return extrude_explicit(dest.x, dest.y, e, f); }

	// Travel to a new XY position. f=0 means use the current value.
	Writer& travel(float x, float y, float f = 0.f) 
		{ return extrude_explicit(x, y, 0, f); }

	Writer& travel(const WipeTower::xy &dest, float f = 0.f) 
		{ return extrude_explicit(dest.x, dest.y, 0.f, f); }

	Writer& extrude(float x, float y, float f = 0.f) {
		float dx = x - m_current_pos.x;
		float dy = y - m_current_pos.y;
		return extrude_explicit(x, y, sqrt(dx*dx+dy*dy) * m_extrusion_flow, f);
	}

	Writer& extrude(const WipeTower::xy &dest, const float f = 0.f) 
		{ return extrude(dest.x, dest.y, f); }

	Writer& deretract(float e, float f = 0.f)
	{
		if (e == 0 && (f == 0 || f == m_current_feedrate))
			return *this;
		m_gcode += "G1 ";
		if (e != 0)
			m_gcode += set_format_E(e);
		if (f != 0 && f != m_current_feedrate)
			m_gcode += set_format_F(f);
		m_gcode += "\n";
		return *this;
	}

	Writer& deretract_move_x(float x, float e, float f = 0.f)
		{ return extrude_explicit(x, m_current_pos.y, e, f); }

	Writer& retract(float e, float f = 0.f)
		{ return retract(-e, f); }

	Writer& z_hop(float hop, float f = 0.f) { 
		m_gcode += std::string("G1") + set_format_Z(m_current_z + hop);
		if (f != 0 && f != m_current_feedrate)
			m_gcode += set_format_F(f);
		m_gcode += "\n";
		return *this;
	}

	// Move to x1, +y_increment,
	// extrude quickly amount e to x2 with feed f.
	Writer& ram(float x1, float x2, float dy, float e, float f) {
		return  travel(x1, m_current_pos.y + dy, f)
			   .extrude_explicit(x2, m_current_pos.y, e);
	}

	Writer& cool(float x1, float x2, float e1, float e2, float f) {
		return  extrude_explicit(x1, m_current_pos.y, e1, f)
			   .extrude_explicit(x2, m_current_pos.y, e2);
	}

	Writer& set_tool(int tool) 
	{
		char buf[64];
		sprintf(buf, "T%d\n", tool);
		m_gcode += buf;
		return *this;
	}

	// Set extruder temperature, don't wait.
	Writer& set_extruder_temp(int temperature, bool wait = false)
	{
		char buf[128];
		sprintf(buf, "M%d S%d\n", wait ? 109 : 104, temperature);
		m_gcode += buf;
		return *this;
	};

	// Set speed factor override percentage
	Writer& speed_override(int speed) {
		char buf[128];
		sprintf(buf, "M220 S%d\n", speed);
		m_gcode += buf;
		return *this;
	};

	// Set digital trimpot motor
	Writer& set_extruder_trimpot(int current)
	{
		char buf[128];
		sprintf(buf, "M907 E%d\n", current);
		m_gcode += buf;
		return *this;
	};

	Writer& flush_planner_queue() { 
		m_gcode += "G4 S0\n"; 
		return *this;
	}

	// Reset internal extruder counter.
	Writer& reset_extruder() { 
		m_gcode += "G92 E0.0\n";
		return *this;
	}

	Writer& comment_with_value(const char *comment, int value)
	{
		char strvalue[15];
		sprintf(strvalue, "%d", value);
		m_gcode += std::string(";") + comment + strvalue + "\n";
		return *this;
	};

	Writer& comment_material(WipeTower::material_type material)
	{
		m_gcode += "; material : ";
		switch (material)
		{
		case WipeTower::PVA:
			m_gcode += "#8 (PVA)";
			break;
		case WipeTower::SCAFF:
			m_gcode += "#5 (Scaffold)";
			break;
		case WipeTower::FLEX:
			m_gcode += "#4 (Flex)";
			break;
		default:
			m_gcode += "DEFAULT (PLA)";
			break;
		}
		m_gcode += "\n";
		return *this;
	};

	Writer& append(const char *text) { m_gcode += text; return *this; }

private:
	WipeTower::xy m_current_pos;
	float    	  m_current_z;
	float 	  	  m_current_feedrate;
	float 	  	  m_extrusion_flow;
	std::string   m_gcode;

	std::string   set_format_X(float x) {
		char buf[64];
		sprintf(buf, " X%.3f", x);
		m_current_pos.x = x;
		return buf;
	}

	std::string   set_format_Y(float y) {
		char buf[64];
		sprintf(buf, " Y%.3f", y);
		m_current_pos.y = y;
		return buf;
	}

	std::string   set_format_Z(float y) {
		char buf[64];
		sprintf(buf, " Z%.3f", y);
		return buf;
	}

	std::string   set_format_E(float e) {
		char buf[64];
		sprintf(buf, " E%.4f", e);
		return buf;
	}

	std::string   set_format_F(float f) {
		char buf[64];
		sprintf(buf, " F%.0f", f);
		m_current_feedrate = f;
		return buf;
	}
};

static inline int randi(int lo, int hi)
{
	int n = hi - lo + 1;
	int i = rand() % n;
	if (i < 0) i = -i;
	return lo + i;
}

WipeTower::material_type WipeTower::parse_material(const char *name)
{
	if (strcasecmp(name, "PLA") == 0)
		return PLA;
	if (strcasecmp(name, "ABS") == 0)
		return ABS;
	if (strcasecmp(name, "PET") == 0)
		return PET;
	if (strcasecmp(name, "HIPS") == 0)
		return HIPS;
	if (strcasecmp(name, "FLEX") == 0)
		return FLEX;
	if (strcasecmp(name, "SCAFF") == 0)
		return SCAFF;
	if (strcasecmp(name, "EDGE") == 0)
		return EDGE;
	if (strcasecmp(name, "NGEN") == 0)
		return NGEN;
	if (strcasecmp(name, "PVA") == 0)
		return PVA;
	return INVALID;
}

std::string WipeTower::FirstLayer(bool sideOnly, float y_offset)
{
	const box_coordinates wipeTower_box(
		m_wipe_tower_pos, 
		m_wipe_tower_width, 
		m_wipe_area * float(m_color_changes) - perimeterWidth / 2);

	Writer writer;
	writer.set_extrusion_flow(extrusion_flow * 1.1f)
		  // Let the writer know the current Z position as a base for Z-hop.
		  .set_z(m_z_pos)
		  .append(
			";-------------------------------------\n"
			"; CP WIPE TOWER FIRST LAYER BRIM START\n");

	// Move with Z hop and prime the extruder 10*perimeterWidth left along the vertical edge of the wipe tower.
	writer.z_hop(zHop, 7200)
		  .travel(wipeTower_box.lu - xy(perimeterWidth * 10.f, 0), 6000)
		  .z_hop(0, 7200)
		  .extrude_explicit(wipeTower_box.ld - xy(perimeterWidth * 10.f, 0), retract, 2400)
		  .feedrate(2100);

	if (sideOnly) {
		float x_offset = 0.f;
		for (size_t i = 0; i < 4; ++ i, x_offset += perimeterWidth)
			writer.travel (wipeTower_box.ld + xy(- x_offset,   y_offset))
				  .extrude(wipeTower_box.lu + xy(- x_offset, - y_offset));
		writer.travel(wipeTower_box.rd + xy(x_offset, y_offset), 7000)
			  .feedrate(2100);
		x_offset = 0.f;
		for (size_t i = 0; i < 4; ++ i, x_offset += perimeterWidth)
			writer.travel (wipeTower_box.rd + xy(x_offset,   y_offset))
				  .extrude(wipeTower_box.ru + xy(x_offset, - y_offset));
	} else {
		// Extrude 4 rounds of a brim around the future wipe tower.
		box_coordinates box(wipeTower_box);
		box.ld += xy(- perimeterWidth / 2, 0);
		box.lu += xy(- perimeterWidth / 2, perimeterWidth);
		box.rd += xy(  perimeterWidth / 2, 0);
		box.ru += xy(  perimeterWidth / 2, perimeterWidth);
		for (size_t i = 0; i < 4; ++ i) {
			writer.travel(box.ld)
				  .extrude(box.lu) .extrude(box.ru)
				  .extrude(box.rd) .extrude(box.ld);
			box.expand(perimeterWidth);
		}
	}

	// Move to the front left corner and wipe along the front edge.
	writer.travel(wipeTower_box.ld, 7000)
		  .travel(wipeTower_box.rd)
		  .travel(wipeTower_box.ld)
		  .append("; CP WIPE TOWER FIRST LAYER BRIM END\n"
			      ";-----------------------------------\n");

	return writer.gcode();
}

std::pair<std::string, WipeTower::xy> WipeTower::Toolchange(
	const int 			tool, 
	const material_type current_material, 
	const material_type new_material, 
	const int 			temperature, 
	const wipe_shape 	shape, 
	const int 			count, 
	const float 		spaceAvailable, 
	const float 		wipeStartY, 
	const bool  		lastInFile, 
	const bool 			colorInit)
{
	box_coordinates cleaning_box(
		m_wipe_tower_pos.x,
		m_wipe_tower_pos.y + wipeStartY,
		m_wipe_tower_width, 
		spaceAvailable - perimeterWidth / 2);

	Writer writer;
	writer.set_extrusion_flow(extrusion_flow)
		  .set_z(m_z_pos)
		  .append(";--------------------\n"
			 	  "; CP TOOLCHANGE START\n")
		  .comment_with_value(" toolchange #", count)
		  .comment_material(current_material)
		  .append(";--------------------\n")
		  .speed_override(100)
		  // Lift for a Z hop.
		  .z_hop(zHop, 7200)
		  // additional retract on move to tower
		  .retract(retract/2, 3600)
		  .travel(((shape == SHAPE_NORMAL) ? cleaning_box.ld : cleaning_box.rd) + xy(perimeterWidth, shape * perimeterWidth), 7200)
		  // Unlift for a Z hop.
		  .z_hop(0, 7200)
		  // Additional retract on move to tower.
		  .deretract(retract/2, 3600)
		  .deretract(retract, 1500)
		  // Increase extruder current for ramming.
		  .set_extruder_trimpot(750)
		  .flush_planner_queue();

	// Ram the hot material out of the melt zone, retract the filament into the cooling tubes and let it cool.
	toolchange_Unload(writer, cleaning_box, current_material, shape, temperature);

	if (! lastInFile) {
		// Change the tool, set a speed override for solube and flex materials.
		toolchange_Change(writer, tool, current_material, new_material);
		toolchange_Load(writer, cleaning_box, current_material, shape, colorInit);
		// Wipe the newly loaded filament until the end of the assigned wipe area.
		toolchange_Wipe(writer, cleaning_box, current_material, shape);
		// Draw a perimeter around cleaning_box and wipe.
		toolchange_Done(writer, cleaning_box, current_material, shape);
	}

	// Reset the extruder current to a normal value.
	writer.set_extruder_trimpot(550)
		  .flush_planner_queue()
		  .reset_extruder()
		  .append("; CP TOOLCHANGE END\n"
	 		      ";------------------\n"
				  "\n\n");

	return std::pair<std::string, xy>(writer.gcode(), writer.pos());
}

// Ram the hot material out of the melt zone, retract the filament into the cooling tubes and let it cool.
void WipeTower::toolchange_Unload(
	Writer 				    &writer,
	const box_coordinates 	&cleaning_box,
	const material_type		 material,
	const wipe_shape 	     shape,
	const int 				 temperature)
{
	float xl = cleaning_box.ld.x + (perimeterWidth / 2);
	float xr = cleaning_box.rd.x - (perimeterWidth / 2);
	float y_step = shape * perimeterWidth;

	writer.append("; CP TOOLCHANGE UNLOAD");

	// Ram the hot material out of the extruder melt zone.
	switch (material)
	{
	case PVA:
   		// ramming          start                    end                  y increment     amount feedrate
		writer.ram(xl + perimeterWidth * 2, xr - perimeterWidth,     y_step * 1.2f, 3,     4000)
			  .ram(xr - perimeterWidth,     xl + perimeterWidth,     y_step * 1.5f, 3,     4500)
			  .ram(xl + perimeterWidth * 2, xr - perimeterWidth * 2, y_step * 1.5f, 3,     4800)
			  .ram(xr - perimeterWidth,     xl + perimeterWidth,     y_step * 1.5f, 3,     5000);
		break;
	case SCAFF:
		writer.ram(xl + perimeterWidth * 2, xr - perimeterWidth,     y_step * 3.f,  3,     4000)
			  .ram(xr - perimeterWidth,     xl + perimeterWidth,     y_step * 3.f,  4,     4600)
			  .ram(xl + perimeterWidth * 2, xr - perimeterWidth * 2, y_step * 3.f,  4.5,   5200);
		break;
	default:
		writer.ram(xl + perimeterWidth * 2, xr - perimeterWidth,     y_step * 1.2f, 1.6f,  4000)
			  .ram(xr - perimeterWidth,     xl + perimeterWidth,     y_step * 1.2f, 1.65f, 4600)
			  .ram(xl + perimeterWidth * 2, xr - perimeterWidth * 2, y_step * 1.2f, 1.74f, 5200);
	}

	// Pull the filament end into a cooling tube.
	writer.retract(15, 5000).retract(50, 5400).retract(15, 3000).deretract(12, 2000);

	if (temperature != 0)
		// Set the extruder temperature, but don't wait.
		writer.set_extruder_temp(temperature, false);

	// Horizontal cooling moves at the following y coordinate:
	writer.travel(writer.x(), writer.y() + y_step * 0.8f, 1600);
	switch (material)
	{
	case PVA:
		writer.cool(xl, xr, 3, -5, 1600)
			  .cool(xl, xr, 5, -5, 2000)
			  .cool(xl, xr, 5, -5, 2200)
			  .cool(xl, xr, 5, -5, 2400)
			  .cool(xl, xr, 5, -5, 2400)
			  .cool(xl, xr, 5, -5, 2400);
		break;
	case SCAFF:
		writer.cool(xl, xr, 3, -5, 1600)
			  .cool(xl, xr, 5, -5, 2000)
			  .cool(xl, xr, 5, -5, 2200)
			  .cool(xl, xr, 5, -5, 2200)
			  .cool(xl, xr, 5, -5, 2400);
		break;
	default:
		writer.cool(xl, xr, 3, -5, 1600)
			  .cool(xl, xr, 5, -5, 2000)
			  .cool(xl, xr, 5, -5, 2400)
			  .cool(xl, xr, 5, -3, 2400);
	}

	writer.flush_planner_queue();
}

// Change the tool, set a speed override for solube and flex materials.
void WipeTower::toolchange_Change(
	Writer 		   &writer,
	const int 		tool, 
	material_type  /* current_material */, 
	material_type 	new_material)
{
	// Speed override for the material. Go slow for flex and soluble materials.
	int speed_override;
	switch (new_material) {
	case PVA:   speed_override = 80; break;
	case SCAFF: speed_override = 35; break;
	case FLEX:  speed_override = 35; break;
	default:    speed_override = 100;
	}
	writer.set_tool(tool)
	      .speed_override(speed_override)
	      .flush_planner_queue();
}

void WipeTower::toolchange_Load(
	Writer                 &writer,
	const box_coordinates  &cleaning_box, 
	const material_type 	/* material */,
	const wipe_shape 		shape,
	const bool 				colorInit)
{
	float xl = cleaning_box.ld.x + perimeterWidth;
	float xr = cleaning_box.rd.x - perimeterWidth;

	writer.append("; CP TOOLCHANGE LOAD\n")
	// Load the filament while moving left / right,
	// so the excess material will not create a blob at a single position.
		  .deretract_move_x(xr, 20, 1400)
		  .deretract_move_x(xl, 40, 3000)
		  .deretract_move_x(xr, 20, 1600)
		  .deretract_move_x(xl, 10, 1000);

	// Extrude first five lines (just three lines if colorInit is set).
	writer.extrude(xr, writer.y(), 1600);
	size_t pass = colorInit ? 1 : 2;
	for (int i = 0; i < pass; ++ i)
		writer.travel (xr, writer.y() + shape * perimeterWidth * 0.85f, 2200)
			  .extrude(xl, writer.y())
			  .travel (xl, writer.y() + shape * perimeterWidth * 0.85f)
			  .extrude(xr, writer.y());

	// Reset the extruder current to the normal value.
	writer.set_extruder_trimpot(550);
}

// Wipe the newly loaded filament until the end of the assigned wipe area.
void WipeTower::toolchange_Wipe(
	Writer                 &writer,
	const box_coordinates  &cleaning_box,
	const material_type 	material,
	const wipe_shape 	    shape)
{
	// Increase flow on first layer, slow down print.
	writer.set_extrusion_flow(extrusion_flow * (is_first_layer() ? 1.18f : 1.f))
		  .append("; CP TOOLCHANGE WIPE\n");
	float wipe_coeff = is_first_layer() ? 0.5f : 1.f;
	float xl = cleaning_box.ld.x + 2.f * perimeterWidth;
	float xr = cleaning_box.rd.x - 2.f * perimeterWidth;
	// Wipe speed will increase up to 4800.
	float wipe_speed = 4200;
	// Y increment per wipe line.
	float dy = shape * perimeterWidth * 0.7f;
	for (bool p = true; ; p = ! p) {
		writer.feedrate((wipe_speed = std::min(4800.f, wipe_speed + 50.f)) * wipe_coeff);
		if (p)
			writer.extrude(xl - perimeterWidth/2, writer.y() + dy)
			      .extrude(xr + perimeterWidth,   writer.y());
		else
			writer.extrude(xl - perimeterWidth,   writer.y() + dy)
				  .extrude(xr + perimeterWidth*2, writer.y());
		writer.feedrate((wipe_speed = std::min(4800.f, wipe_speed + 50.f)) * wipe_coeff)
			  .extrude(xr + perimeterWidth, writer.y() + dy)
			  .extrude(xl - perimeterWidth, writer.y());
		if ((shape == SHAPE_NORMAL) ?
			(writer.y() > cleaning_box.lu.y - perimeterWidth) :
			(writer.y() < cleaning_box.ld.y + perimeterWidth))
			// Next wipe line does not fit the cleaning box.
			break;
	}
	// Reset the extrusion flow.
	writer.set_extrusion_flow(extrusion_flow);
}

// Draw a perimeter around cleaning_box and wipe.
void WipeTower::toolchange_Done(
	Writer 					&writer,
	const box_coordinates 	&cleaning_box,
	const material_type 	/* material */, 
	const wipe_shape 		 shape)
{
	box_coordinates box = cleaning_box;
	if (shape == SHAPE_REVERSED) {
		std::swap(box.lu, box.ld);
		std::swap(box.ru, box.rd);
	}
	// Draw a perimeter around cleaning_box.
	writer.travel(box.lu, 7000)
		  .extrude(box.ld, 3200).extrude(box.rd)
		  .extrude(box.ru).extrude(box.lu)
		  // Wipe the nozzle.
		  .travel(box.ru, 7200)
		  .travel(box.lu)
		  .feedrate(6000);
}

std::string WipeTower::Perimeter(int order, int total, int Layer, bool afterToolchange, int firstLayerOffset)
{
	Writer writer;
	writer.set_extrusion_flow(extrusion_flow)
		  .set_z(m_z_pos)
		  .append(";--------------------\n"
				  "; CP EMPTY GRID START\n")
		  .comment_with_value(" layer #", Layer);

	// Slow down on the 1st layer.
	float speed_factor = is_first_layer() ? 0.5f : 1.f;

	box_coordinates _p  = _boxForColor(order);
	{
		box_coordinates _to = _boxForColor(total);
		_p.ld.y += firstLayerOffset;
		_p.rd.y += firstLayerOffset;
		_p.lu = _to.lu; _p.ru = _to.ru;
	}

	if (! afterToolchange)
		// Jump with retract to _p.ld + a random shift in +x.
		writer.retract(retract * 1.5f, 3600)
			  .z_hop(zHop, 7200)
			  .travel(_p.ld.x + randi(5, 20), _p.ld.y, 7000)
			  .z_hop(0, 7200)
			  .extrude_explicit(_p.ld, retract * 1.5f, 3600);

	box_coordinates box = _p;
	writer.extrude(box.lu, 2400 * speed_factor)
		  .extrude(box.ru)
		  .extrude(box.rd)
		  .extrude(box.ld + xy(perimeterWidth / 2, 0));

	box.expand(- perimeterWidth / 2);
	writer.extrude(box.lu, 3200 * speed_factor)
		  .extrude(box.ru)
		  .extrude(box.rd)
		  .extrude(box.ld + xy(perimeterWidth / 2, 0))
		  .extrude(box.ld + xy(perimeterWidth / 2, perimeterWidth / 2));

	writer.extrude(_p.ld + xy(perimeterWidth * 3,   perimeterWidth), 2900 * speed_factor)
	      .extrude(_p.lu + xy(perimeterWidth * 3, - perimeterWidth))
		  .extrude(_p.lu + xy(perimeterWidth * 6, - perimeterWidth))
		  .extrude(_p.ld + xy(perimeterWidth * 6,   perimeterWidth));

	if (_p.lu.y - _p.ld.y > 4) {
		// Extrude three zig-zags.
		writer.feedrate(3200 * speed_factor);
		float step = (m_wipe_tower_width - perimeterWidth * 12.f) / 12.f;
		for (size_t i = 0; i < 3; ++ i) {
			writer.extrude(writer.x() + step, _p.ld.y + perimeterWidth * 8);
			writer.extrude(writer.x()       , _p.lu.y - perimeterWidth * 8);
			writer.extrude(writer.x() + step, _p.lu.y - perimeterWidth    );
			writer.extrude(writer.x() + step, _p.lu.y - perimeterWidth * 8);
			writer.extrude(writer.x()       , _p.ld.y + perimeterWidth * 8);
			writer.extrude(writer.x() + step, _p.ld.y + perimeterWidth    );
		}
	}

	writer.extrude(_p.ru + xy(- perimeterWidth * 6, - perimeterWidth), 2900 * speed_factor)
		  .extrude(_p.ru + xy(- perimeterWidth * 3, - perimeterWidth))
		  .extrude(_p.rd + xy(- perimeterWidth * 3,   perimeterWidth))
		  .extrude(_p.rd + xy(- perimeterWidth,       perimeterWidth))
       	  // Wipe along the front side of the current wiping box.
		  .travel(_p.ld + xy(  perimeterWidth, perimeterWidth / 2), 7200)
		  .travel(_p.rd + xy(- perimeterWidth, perimeterWidth / 2))
		  .append("; CP EMPTY GRID END\n"
			      ";------------------\n\n\n\n\n\n\n");

	return writer.gcode();
}

WipeTower::box_coordinates WipeTower::_boxForColor(int order) const
{
	return box_coordinates(m_wipe_tower_pos.x, m_wipe_tower_pos.y + m_wipe_area * order - perimeterWidth / 2, m_wipe_tower_width, perimeterWidth);
}

}; // namespace PrusaSingleExtruderMM

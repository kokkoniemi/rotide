defmodule Router do
  def handle(request) do
    method = request.method
    path = request.path

    case method do
      :get -> {200, path}
      :post -> {201, ""}
      _ -> {405, ""}
    end
  end
end

routes = [
  {"/health", &Router.handle/1},
  {"/status", &Router.handle/1}
]
